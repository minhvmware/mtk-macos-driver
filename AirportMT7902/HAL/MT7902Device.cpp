// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902Device.hpp"
#include "../Net80211/MT7902Channels.hpp"

namespace mt7902 {

static constexpr int kDrvOwnRetries = 10;

MT7902Device::MT7902Device(IMT7902Bus &bus)
    : mBus(bus), mDma(bus), mMcu(bus, mDma), mFw(bus, mMcu), mMac(bus, mDma) {
    mMcu.setObserver(this);
    mRxDataScratch = static_cast<uint8_t *>(kallocZero(MT7902_RX_BUF_SIZE));
}

MT7902Device::~MT7902Device() {
    shutdown();
    if (mRxDataScratch) {
        kfreeSized(mRxDataScratch, MT7902_RX_BUF_SIZE);
        mRxDataScratch = nullptr;
    }
}

int MT7902Device::acquireDriverOwn() {
    for (int i = 0; i < kDrvOwnRetries; ++i) {
        mBus.wr(MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN);
        // Always wait 2.5ms after CLR_OWN: physical MT7902 PCIe link may have ASPM L0s/L1
        // enabled even when hypervisor/VFIO virtualizes LnkCap as 0.
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

    // Steps 3..8: Retry up to 8 times (matching Linux MT792x_MCU_INIT_RETRY_COUNT)
    // to cleanly survive concurrent MT7902 USB Bluetooth WMT firmware download/reset at boot.
    static constexpr int kMcuInitRetries = 8;
    for (int attempt = 0; attempt < kMcuInitRetries; ++attempt) {
        if (attempt > 0) {
            MTK_LOG("MT7902 probeAndInit retry %d/%d after err=%d",
                    attempt + 1, kMcuInitRetries, err);
            (void)releaseDriverOwn();
            mBus.sleepMs(200);
            (void)acquireDriverOwn();
            mBus.sleepMs(300);
        }

        // Step 3: Reset Wi-Fi subsystem and force normal firmware mode (matching __mt7921_init_hardware)
        err = wfsysReset();
        if (err != kOk)
            continue;

        mBus.wr(MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
        uint32_t staleMcuCmd = mBus.rr(MT_MCU_CMD);
        if (staleMcuCmd)
            mBus.wr(MT_MCU_CMD, staleMcuCmd);

        mBus.wr(MT_WFDMA0_HOST_INT_ENA, 0);
        mBus.wr(MT_PCIE_MAC_INT_ENABLE, 0xff);

        // Step 4: Initialize MT7902 WFDMA0 rings (Ring 15 MCU-WM, Ring 0 shared 512, no WA)
        err = mDma.init();
        if (err != kOk)
            continue;
        // Mask WFDMA0 host IRQs during synchronous polling probeAndInit() while keeping
        // MT_PCIE_MAC_INT_ENABLE=0xff (matching Linux mt7921_pci_probe).
        mBus.wr(MT_WFDMA0_HOST_INT_ENA, 0);
        mBus.wr(MT_PCIE_MAC_INT_ENABLE, 0xff);
        {
            uint32_t sta = mBus.rr(MT_WFDMA0_HOST_INT_STA);
            if (sta)
                mBus.wr(MT_WFDMA0_HOST_INT_STA, sta);
        }

        // Step 5: Band0 driver own + disable PCIe L0s before firmware download
        err = driverOwnBand0();
        if (err != kOk) {
            mDma.cleanup();
            continue;
        }
        mBus.rmw(MT_PCIE_MAC_PM, MT_PCIE_MAC_PM_L0S_DIS, MT_PCIE_MAC_PM_L0S_DIS);

        // Step 6: Boot MT7902 ROM patch + WM RAM firmware
        err = mFw.loadEmbeddedAndBoot(mFwVersion);
        if (err != kOk) {
            mDma.cleanup();
            continue;
        }
        mDma.txCleanup(kTxFwdl);

        // Step 7: Query NIC capabilities (permanent MAC address, 2G/5G/6G, NSS)
        err = mMcu.getNicCapability(mCap);
        if (err != kOk) {
            MTK_ERR("Failed to query MT7902 NIC capabilities (%d)", err);
            mDma.cleanup();
            continue;
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

        // Enable firmware debug log events during boot (matching Linux mt7921_run_firmware)
        (void)mMcu.setFwLog2Host(1);

        // Step 8: Configure EEPROM EFUSE mode + MAC hardware registers.
        if (mBus.rr(MT_MCU_CMD) & MT_MCU_CMD_STOP_DMA) {
            MTK_LOG("CONNSYS signaled STOP_DMA during init (mcucmd=0x%x); retrying",
                    mBus.rr(MT_MCU_CMD));
            err = kErrIO;
            mDma.cleanup();
            continue;
        }
        int efuseErr = mMcu.setEepromEfuseMode();
        if (efuseErr == kErrIO && (mBus.rr(MT_MCU_CMD) & MT_MCU_CMD_STOP_DMA)) {
            err = kErrIO;
            mDma.cleanup();
            continue;
        }
        if (efuseErr != kOk) {
            MTK_LOG("setEepromEfuseMode returned %d; verifying MCU liveness via RTS threshold",
                    efuseErr);
        }
        err = mMac.initHardware();
        if (err != kOk) {
            mDma.cleanup();
            continue;
        }
        err = mMcu.setRtsThreshold(0x92b, 0);
        if (err != kOk) {
            MTK_ERR("MCU unresponsive after EFUSE/MAC init (%d), retrying init", err);
            mDma.cleanup();
            continue;
        }
        if (efuseErr != kOk) {
            MTK_LOG("MT7902 MCU responsive (EFUSE already active via CONNSYS)");
        }

        // MT7902 hardware quirk (init.c:307): keep deep sleep disabled by default for stability
        (void)mMcu.setDeepSleep(false);

        // Re-enable WFDMA0 host IRQs now that synchronous probeAndInit() succeeded
        uint32_t pendingInt = mBus.rr(MT_WFDMA0_HOST_INT_STA);
        if (pendingInt)
            mBus.wr(MT_WFDMA0_HOST_INT_STA, pendingInt);
        mDma.enableInterrupts();

        mInitialized = true;
        return kOk;
    }

    MTK_ERR("MT7902 probeAndInit exhausted %d retries (last err=%d)", kMcuInitRetries, err);
    return err;
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
    if (mSuspended) {
        int r = resume();
        if (r != kOk)
            return r;
    }
    if (mRadioRunning)
        return kOk;

    int err = mMcu.setMacEnable(0, true);
    if (err != kOk) {
        MTK_ERR("setMacEnable(true) failed (%d), attempting hardware recovery", err);
        shutdown();
        err = probeAndInit();
        if (err != kOk) {
            MTK_ERR("Hardware recovery during startRadio failed: %d", err);
            return err;
        }
        err = mMcu.setMacEnable(0, true);
        if (err != kOk) {
            MTK_ERR("setMacEnable(true) still failed after recovery: %d", err);
            return err;
        }
    }

    // Populate regulatory channel domain in firmware (matching __mt7921_start);
    // must run on every startRadio() including after MAC rotation via setMacAddress().
    ScanChannelEntry chList[64];
    size_t chCount = buildScanChannelList(mCap.has2Ghz, mCap.has5Ghz, mCap.has6Ghz, chList, 64);
    (void)mMcu.setChannelDomain("00", chList, chCount);

    ChannelConfig defCh;
    defCh.primaryChannel = 1;
    defCh.centerChannel = 1;
    defCh.band = 0;
    defCh.bw = CMD_CBW_20MHZ;
    (void)mMcu.setChannel(defCh, mCap.antennaMask);
    mMac.setTiming(true, 9);

    err = mMcu.uniAddDev(0, 0, 0, mCap.macAddr, true);
    if (err != kOk) {
        MTK_ERR("uniAddDev(true) failed: %d", err);
        return err;
    }

    // Enable MT7902 RX filter + clear beacon/probe-response filter both via MCU and RMAC RFCR
    (void)mMcu.setRxFilter(MT7902_FILTER_ENABLE | MT7902_FILTER_OTHER_BSS, 0, 0);
    (void)mMcu.setRxFilter(0, MTK_BIT(1), MT_WF_RFCR_DROP_OTHER_BEACON);
    mBus.clear(MT_WF_RFCR(0),
               MT_WF_RFCR_DROP_OTHER_BEACON | MT_WF_RFCR_DROP_OTHER_BSS | MT_WF_RFCR_DROP_OTHER_UC);

    mRadioRunning = true;
    MTK_LOG("MT7902 radio enabled successfully");
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

void MT7902Device::handleRxPacket(const uint8_t *buf, size_t len) {
    ParsedRxFrame meta;
    if (!MT7902Mac::parseRxDescriptor(buf, len, mCap.antennaMask, meta) || !meta.valid) {
        mRxParseFailCount++;
        return;
    }

    mRxNormalCount++;
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

void MT7902Device::drainRxData() {
    if (!mRxDataScratch)
        return;
    size_t len = 0;
    for (int i = 0; i < 512; ++i) {
        if (mDma.rxDequeue(kRxData, mRxDataScratch, MT7902_RX_BUF_SIZE, len) != kOk)
            break;
        handleRxPacket(mRxDataScratch, len);
    }
}

void MT7902Device::serviceInterrupt() {
    if (!mInitialized)
        return;

    IrqStatus st = mDma.handleInterrupt();
    if (st.mcuStopDma) {
        MTK_ERR("MT7902 MCU signaled STOP_DMA; triggering hardware recovery");
        (void)resetHardware();
        return;
    }
    if (st.txDone) {
        mDma.txCleanup(kTxMcuWm);
        mDma.txCleanup(kTxFwdl);
        mDma.txCleanup(kTxBand0);
    }
    drainRxData();
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
                                 bool protect, bool noAck, bool fiveGhz) {
    if (!mRadioRunning)
        return kErrIO;
    TxPacketOptions opts;
    opts.wcid = wcid;
    opts.is8023 = false;
    opts.qos = false;
    opts.protect = protect;
    opts.noAck = noAck;
    opts.fixedRate = true;
    // Lowest basic rate per band: CCK 1 Mbps (0x0000) on 2.4 GHz, OFDM 6 Mbps (0x004b) on 5/6 GHz
    opts.fixedRateVal = fiveGhz
                            ? static_cast<uint16_t>((static_cast<uint16_t>(MT_PHY_TYPE_OFDM) << 6) | 0x0bU)
                            : 0x0000U;
    if (len >= 10)
        opts.multicast = (frame[4] & 1U) != 0;
    return mMac.transmitFrame(frame, len, opts);
}

void MT7902Device::onScanComplete(uint8_t seqNum) {
    drainRxData();
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

void MT7902Device::onRxNormalPacket(const uint8_t *data, size_t len) {
    handleRxPacket(data, len);
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
