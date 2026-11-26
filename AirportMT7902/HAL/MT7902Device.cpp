// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902Device.hpp"

namespace mt7902 {

static constexpr int kDrvOwnRetries = 10;

MT7902Device::MT7902Device(IMT7902Bus &bus)
    : mBus(bus), mDma(bus), mMcu(bus, mDma), mFw(bus, mMcu), mMac(bus, mDma) {
    mMcu.setObserver(this);
}

MT7902Device::~MT7902Device() {
    shutdown();
}

int MT7902Device::acquireDriverOwn() {
    for (int i = 0; i < kDrvOwnRetries; ++i) {
        mBus.wr(MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN);
        if (mBus.isAspmSupported())
            mBus.delayUs(2500);
        if (mBus.pollMs(MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_OWN_SYNC, 0, 50, 1))
            return kOk;
    }
    MTK_ERR("MT7902 acquireDriverOwn failed (LPCTL=0x%08x)", mBus.rr(MT_CONN_ON_LPCTL));
    return kErrIO;
}

int MT7902Device::releaseDriverOwn() {
    for (int i = 0; i < kDrvOwnRetries; ++i) {
        mBus.wr(MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_SET_OWN);
        if (mBus.pollMs(MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_OWN_SYNC, PCIE_LPCR_HOST_OWN_SYNC, 50, 1))
            return kOk;
    }
    MTK_ERR("MT7902 releaseDriverOwn failed");
    return kErrIO;
}

int MT7902Device::driverOwnBand0() {
    mBus.l1Wr(MT_TOP_LPCR_HOST_BAND0, MT_TOP_LPCR_HOST_DRV_OWN);
    if (!mBus.pollL1Ms(MT_TOP_LPCR_HOST_BAND0, MT_TOP_LPCR_HOST_FW_OWN, 0, 500, 1)) {
        MTK_ERR("MT7902 band0 driver own timeout");
        return kErrIO;
    }
    return kOk;
}

int MT7902Device::wfsysReset() {
    uint32_t reg = mBus.remapL1(MT_WFSYS_SW_RST_B);
    uint32_t val = mBus.barRead32(reg);
    mBus.barWrite32(reg, val & ~WFSYS_SW_RST_B);
    mBus.sleepMs(50);
    mBus.barWrite32(reg, val | WFSYS_SW_RST_B);

    if (!mBus.pollL1Ms(MT_WFSYS_SW_RST_B, WFSYS_SW_INIT_DONE, WFSYS_SW_INIT_DONE, 500, 1)) {
        MTK_ERR("MT7902 WFSYS reset timeout");
        return kErrTimeout;
    }
    return kOk;
}

int MT7902Device::probeAndInit() {
    if (mInitialized)
        return kOk;

    // Step 1: PM ownership cycle (matching mt7921_pci_probe)
    int err = releaseDriverOwn();
    if (err != kOk)
        return err;
    err = acquireDriverOwn();
    if (err != kOk)
        return err;

    // Step 2: Verify chip ID == 0x7902 via L1 remap
    uint16_t chipId = static_cast<uint16_t>(mBus.l1Rr(MT_HW_CHIPID) & 0xffffU);
    uint32_t rev = mBus.l1Rr(MT_HW_REV) & 0xffU;
    mChipRev = (static_cast<uint32_t>(chipId) << 16) | rev;
    MTK_LOG("Detected MediaTek ASIC chip=0x%04x rev=0x%08x", chipId, mChipRev);
    if (chipId != kMT7902DeviceID) {
        MTK_ERR("Unexpected chip ID 0x%04x (expected 0x7902)", chipId);
        return kErrNoDev;
    }

    // Step 3: Reset Wi-Fi subsystem
    err = wfsysReset();
    if (err != kOk)
        return err;

    mBus.wr(MT_WFDMA0_HOST_INT_ENA, 0);
    mBus.wr(MT_PCIE_MAC_INT_ENABLE, 0xff);

    // Step 4: Initialize MT7902 WFDMA0 rings (Ring 15 MCU-WM, Ring 0 shared 512, no WA)
    err = mDma.init();
    if (err != kOk)
        return err;

    // Step 5: Band0 driver own + disable PCIe L0s before firmware download
    err = driverOwnBand0();
    if (err != kOk) {
        mDma.cleanup();
        return err;
    }
    mBus.rmw(MT_PCIE_MAC_PM, MT_PCIE_MAC_PM_L0S_DIS, MT_PCIE_MAC_PM_L0S_DIS);

    // Step 6: Boot MT7902 ROM patch + WM RAM firmware
    err = mFw.loadEmbeddedAndBoot(mFwVersion);
    if (err != kOk) {
        mDma.cleanup();
        return err;
    }
    mDma.txCleanup(kTxFwdl);

    // Step 7: Query NIC capabilities (permanent MAC address, 2G/5G/6G, NSS)
    err = mMcu.getNicCapability(mCap);
    if (err != kOk) {
        MTK_ERR("Failed to query MT7902 NIC capabilities (%d)", err);
        mDma.cleanup();
        return err;
    }

    if (!mCap.macValid) {
        // Fallback locally administered MAC if EFUSE returned zeros
        mCap.macAddr[0] = 0x02;
        mCap.macAddr[1] = 0x79;
        mCap.macAddr[2] = 0x02;
        mCap.macAddr[3] = 0x14;
        mCap.macAddr[4] = 0xc3;
        mCap.macAddr[5] = 0x01;
        mCap.macValid = true;
    }

    MTK_LOG("MT7902 MAC %02x:%02x:%02x:%02x:%02x:%02x | 2G=%d 5G=%d 6G=%d NSS=%u HE=%d",
            mCap.macAddr[0], mCap.macAddr[1], mCap.macAddr[2],
            mCap.macAddr[3], mCap.macAddr[4], mCap.macAddr[5],
            mCap.has2Ghz, mCap.has5Ghz, mCap.has6Ghz, mCap.nss, mCap.supportsHe);

    // Step 8: Configure EEPROM EFUSE mode + MAC hardware registers
    err = mMcu.setEepromEfuseMode();
    if (err != kOk) {
        mDma.cleanup();
        return err;
    }
    err = mMac.initHardware();
    if (err != kOk) {
        mDma.cleanup();
        return err;
    }

    // MT7902 hardware quirk (init.c:307): keep deep sleep disabled by default for stability
    (void)mMcu.setDeepSleep(false);

    mInitialized = true;
    return kOk;
}

void MT7902Device::shutdown() {
    if (!mInitialized)
        return;
    stopRadio();
    mMac.releaseAllTokens();
    (void)acquireDriverOwn();
    mDma.cleanup();
    (void)wfsysReset();
    mInitialized = false;
}

int MT7902Device::startRadio() {
    if (!mInitialized)
        return kErrNoDev;
    if (mRadioRunning)
        return kOk;

    int err = mMcu.setMacEnable(0, true);
    if (err != kOk)
        return err;

    err = mMcu.uniAddDev(0, 0, 0, mCap.macAddr, true);
    if (err != kOk)
        return err;

    ChannelConfig defCh;
    defCh.primaryChannel = 1;
    defCh.centerChannel = 1;
    defCh.band = 0;
    defCh.bw = CMD_CBW_20MHZ;
    (void)mMcu.setChannel(defCh, mCap.antennaMask);

    mRadioRunning = true;
    return kOk;
}

int MT7902Device::stopRadio() {
    if (!mRadioRunning)
        return kOk;
    (void)mMcu.uniAddDev(0, 0, 0, mCap.macAddr, false);
    (void)mMcu.setMacEnable(0, false);
    mRadioRunning = false;
    return kOk;
}

int MT7902Device::setMacAddress(const uint8_t mac[6]) {
    if (!mac || (mac[0] & 0x01U))
        return kErrInvalid;
    if (memcmp(mCap.macAddr, mac, 6) == 0)
        return kOk;
    bool wasRunning = mRadioRunning;
    if (wasRunning)
        (void)stopRadio();
    memcpy(mCap.macAddr, mac, 6);
    mCap.macValid = true;
    if (wasRunning)
        return startRadio();
    return kOk;
}

int MT7902Device::suspend() {
    if (!mInitialized || mSuspended)
        return kOk;
    mSuspended = true;
    (void)acquireDriverOwn();
    (void)mMcu.setHifSuspend(true);
    (void)mMcu.setDeepSleep(true);
    (void)mDma.disable(false);
    mDma.disableInterrupts();
    (void)releaseDriverOwn();
    return kOk;
}

int MT7902Device::resume() {
    if (!mInitialized || !mSuspended)
        return kOk;
    int err = acquireDriverOwn();
    if (err != kOk)
        return resetHardware();

    if (mDma.needsReinit()) {
        err = mDma.reset(false);
        if (err != kOk)
            return resetHardware();
    } else {
        mDma.enableInterrupts();
        mBus.set(MT_WFDMA0_GLO_CFG,
                 MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);
    }

    (void)mMcu.setDeepSleep(false);
    (void)mMcu.setHifSuspend(false);
    mSuspended = false;
    return kOk;
}

int MT7902Device::resetHardware() {
    MTK_LOG("Performing MT7902 hardware recovery reset");
    bool wasRunning = mRadioRunning;
    shutdown();
    int err = probeAndInit();
    if (err == kOk && wasRunning)
        err = startRadio();
    return err;
}

void MT7902Device::drainRxData() {
    uint8_t buf[MT7902_RX_BUF_SIZE];
    size_t len = 0;
    for (int i = 0; i < 64; ++i) {
        if (mDma.rxDequeue(kRxData, buf, sizeof(buf), len) != kOk)
            break;

        ParsedRxFrame meta;
        if (!MT7902Mac::parseRxDescriptor(buf, len, mCap.antennaMask, meta) || !meta.valid)
            continue;

        mMac.recordRxBytes(meta.payloadLen);
        if (meta.rssi < 0 && meta.rssi > -120)
            mLastRssi = meta.rssi;

        const uint8_t *payload = buf + meta.payloadOffset;
        if (mRxCb) {
            if (meta.is8023)
                mRxCb->onRxEthernetFrame(payload, meta.payloadLen, meta);
            else
                mRxCb->onRx80211Frame(payload, meta.payloadLen, meta);
        }
    }
}

void MT7902Device::serviceInterrupt() {
    if (!mInitialized)
        return;

    IrqStatus st = mDma.handleInterrupt();
    if (st.txDone) {
        mDma.txCleanup(kTxMcuWm);
        mDma.txCleanup(kTxFwdl);
        mDma.txCleanup(kTxBand0);
    }
    // Drain RX Ring 2 (802.11 Beacons/Mgmt/Data) BEFORE RX Ring 0 (MCU events)
    // so in-flight scan beacons are delivered before MCU_EVENT_SCAN_DONE fires.
    if (st.rxData || st.mcuWakeRx)
        drainRxData();
    if (st.rxMcuEvent)
        mMcu.drainRxEvents();
}

int MT7902Device::sendEthernetFrame(const uint8_t *frame, size_t len, uint16_t wcid,
                                    bool qos, bool protect) {
    if (!mRadioRunning)
        return kErrIO;
    TxPacketOptions opts;
    opts.wcid = wcid;
    opts.is8023 = true;
    opts.qos = qos;
    opts.protect = protect;
    opts.ac = 2; // Best Effort
    opts.tid = 0;
    return mMac.transmitFrame(frame, len, opts);
}

int MT7902Device::send80211Frame(const uint8_t *frame, size_t len, uint16_t wcid,
                                 bool protect, bool noAck) {
    if (!mRadioRunning)
        return kErrIO;
    TxPacketOptions opts;
    opts.wcid = wcid;
    opts.is8023 = false;
    opts.qos = false;
    opts.protect = protect;
    opts.noAck = noAck;
    opts.fixedRate = true;
    opts.fixedRateVal = 0; // Lowest basic rate for management frames
    if (len >= 10)
        opts.multicast = (frame[4] & 1U) != 0;
    return mMac.transmitFrame(frame, len, opts);
}

void MT7902Device::onScanComplete(uint8_t seqNum) {
    if (mRxCb)
        mRxCb->onScanComplete(seqNum);
}

void MT7902Device::onBeaconLoss(uint8_t bssIdx, uint8_t reason) {
    if (mRxCb)
        mRxCb->onBeaconLoss(bssIdx, reason);
}

void MT7902Device::onTxFreePacket(const uint8_t *data, size_t len) {
    (void)mMac.handleTxFree(data, len);
}

void MT7902Device::onTxStatusPacket(const uint8_t *data, size_t len) {
    (void)data;
    (void)len;
}

void MT7902Device::onRssiNotify(int32_t rssi) {
    if (rssi < 0 && rssi > -120)
        mLastRssi = rssi;
}

void MT7902Device::onFirmwareAssert() {
    if (mRxCb)
        mRxCb->onFirmwareResetNeeded();
}

} // namespace mt7902
