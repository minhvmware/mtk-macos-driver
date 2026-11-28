// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Top-level MT7902 PCIe hardware lifecycle & packet/event multiplexer

#ifndef MT7902_DEVICE_HPP
#define MT7902_DEVICE_HPP

#include "../../include/MT7902Common.hpp"
#include "MT7902Bus.hpp"
#include "MT7902Dma.hpp"
#include "MT7902Firmware.hpp"
#include "MT7902Mac.hpp"
#include "MT7902Mcu.hpp"
#include "MT7902Regs.hpp"

namespace mt7902 {

class IMT7902RxCallback {
public:
    virtual ~IMT7902RxCallback() = default;
    virtual void onRxBatchBegin() {}
    virtual void onRxBatchComplete() {}
    virtual void onRxEthernetFrame(const uint8_t *ethFrame, size_t len, const ParsedRxFrame &meta) = 0;
    virtual void onRx80211Frame(const uint8_t *dot11Frame, size_t len, const ParsedRxFrame &meta) = 0;
    virtual void onScanComplete(uint8_t seqNum) = 0;
    virtual void onBeaconLoss(uint8_t bssIdx, uint8_t reason) = 0;
    virtual void onFirmwareResetNeeded() = 0;
};

class MT7902Device : public IMcuEventObserver {
public:
    explicit MT7902Device(IMT7902Bus &bus);
    ~MT7902Device() override;

    void setRxCallback(IMT7902RxCallback *cb) { mRxCb = cb; }

    // Hardware lifecycle (matches Linux mt7921_pci_probe / remove / suspend / resume)
    int probeAndInit();
    void shutdown();
    int startRadio();
    int stopRadio();
    int setMacAddress(const uint8_t mac[6]);
    int suspend();
    int resume();
    int resetHardware();

    // Driver-own / Firmware-own PM handshake
    int acquireDriverOwn();
    int releaseDriverOwn();
    int wfsysReset();

    // Interrupt service routine bottom-half (runs on IOWorkLoop / IO80211WorkQueue)
    void serviceInterrupt();

    // Packet transmission
    int sendEthernetFrame(const uint8_t *frame, size_t len, uint16_t wcid, bool qos, bool protect,
                          bool hwAmsdu = false, bool kick = true);
    void flushTxData() { mDma.txKick(kTxBand0); }
    int send80211Frame(const uint8_t *frame, size_t len, uint16_t wcid, bool protect,
                       bool noAck = false, bool fiveGhz = false);

    // IMcuEventObserver overrides
    void onScanComplete(uint8_t seqNum) override;
    void onBeaconLoss(uint8_t bssIdx, uint8_t reason) override;
    void onTxFreePacket(const uint8_t *data, size_t len) override;
    void onTxStatusPacket(const uint8_t *data, size_t len) override;
    void onRxNormalPacket(const uint8_t *data, size_t len) override;
    void onRssiNotify(int32_t rssi) override;
    void onFirmwareAssert() override;

    // Accessors
    IMT7902Bus &bus() { return mBus; }
    MT7902Dma &dma() { return mDma; }
    MT7902Mcu &mcu() { return mMcu; }
    MT7902Mac &mac() { return mMac; }
    const NicCapability &capability() const { return mCap; }
    uint32_t chipRev() const { return mChipRev; }
    const char *firmwareVersion() const { return mFwVersion; }
    bool isInitialized() const { return mInitialized; }
    bool isRadioRunning() const { return mRadioRunning; }
    int32_t lastRssi() const { return mLastRssi; }

private:
    int driverOwnBand0();
    void handleRxPacket(const uint8_t *buf, size_t len);
    uint32_t drainRxData();

    IMT7902Bus &mBus;
    MT7902Dma mDma;
    MT7902Mcu mMcu;
    MT7902Firmware mFw;
    MT7902Mac mMac;
    IMT7902RxCallback *mRxCb = nullptr;

    NicCapability mCap;
    uint32_t mChipRev = 0;
    char mFwVersion[32] = "MT7902-FW";
    bool mInitialized = false;
    bool mRadioRunning = false;
    bool mSuspended = false;
    bool mDrainingRxData = false;
    int32_t mLastRssi = -65;
    uint32_t mRxNormalCount = 0;
    uint32_t mRxParseFailCount = 0;
    uint8_t *mRxDataScratch = nullptr;
};

} // namespace mt7902

#endif // MT7902_DEVICE_HPP
