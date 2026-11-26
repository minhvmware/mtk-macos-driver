// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Connac2 MCU message transport & MT7902 control commands

#ifndef MT7902_MCU_HPP
#define MT7902_MCU_HPP

#include "../../include/MT7902Common.hpp"
#include "MT7902Bus.hpp"
#include "MT7902Dma.hpp"
#include "MT7902McuTypes.hpp"

namespace mt7902 {

struct NicCapability {
    uint8_t macAddr[6] = {};
    bool macValid = false;
    bool has2Ghz = true;
    bool has5Ghz = true;
    bool has6Ghz = false;
    uint8_t nss = 1;
    uint8_t antennaMask = 1;
    uint8_t maxBw = CMD_CBW_80MHZ;
    bool supportsHe = true;
    uint64_t chipCap = 0;
};

struct ChannelConfig {
    uint8_t primaryChannel = 1;
    uint8_t centerChannel = 1;
    uint8_t centerChannel2 = 0;
    uint8_t band = 0; // 0: 2.4GHz, 1: 5GHz, 2: 6GHz
    uint8_t bw = CMD_CBW_20MHZ;
    bool offchannel = false;
};

struct ScanRequestParams {
    uint8_t bssIdx = 0;
    uint8_t bandIdx = 0;
    bool active = true;
    uint8_t ssidCount = 0;
    uint8_t ssids[4][32] = {};
    uint8_t ssidLens[4] = {};
    uint8_t bssid[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t channelCount = 0;
    ScanChannelEntry channels[64] = {};
    uint16_t dwellMs = 0;
};

struct StaUpdateParams {
    uint8_t bssIdx = 0;
    uint16_t wcid = 1;
    uint16_t aid = 0;
    uint8_t peerMac[6] = {};
    uint8_t state = CONN_STATE_CONNECT;
    bool qos = true;
    bool enable = true;
    bool newly = true;
    int8_t rssi = -50;
};

struct KeyInstallParams {
    uint8_t bssIdx = 0;
    uint16_t wcid = 1;
    uint8_t cipher = MCU_CIPHER_AES_CCMP;
    uint8_t keyIdx = 0;
    uint8_t keyLen = 16;
    uint8_t key[32] = {};
    bool setKey = true;
};

class IMcuEventObserver {
public:
    virtual ~IMcuEventObserver() = default;
    virtual void onScanComplete(uint8_t seqNum) = 0;
    virtual void onBeaconLoss(uint8_t bssIdx, uint8_t reason) = 0;
    virtual void onTxFreePacket(const uint8_t *data, size_t len) = 0;
    virtual void onTxStatusPacket(const uint8_t *data, size_t len) = 0;
    virtual void onRssiNotify(int32_t rssi) = 0;
    virtual void onFirmwareAssert() = 0;
};

class MT7902Mcu {
public:
    MT7902Mcu(IMT7902Bus &bus, MT7902Dma &dma);

    void setObserver(IMcuEventObserver *obs) { mObserver = obs; }

    // Wire-format builder (exposed for unit testing & reuse)
    size_t buildMessage(uint32_t cmd, const void *payload, size_t payloadLen,
                        uint8_t *outBuf, size_t outCap, uint8_t &outSeq);

    // Send command & optionally wait synchronously for matching seq response
    int sendMessage(uint32_t cmd, const void *payload, size_t payloadLen,
                    bool waitResp, uint8_t *respBuf = nullptr,
                    size_t respCap = 0, size_t *respLen = nullptr,
                    uint32_t timeoutMs = 3000);

    // Process one raw packet pulled from RX Ring 0 (MT7902 shared event/tx-free ring)
    void processRxRing0Packet(const uint8_t *pkt, size_t len);

    // Poll RX Ring 0 and feed all pending packets into processRxRing0Packet()
    void drainRxEvents();

    // Firmware boot & control commands
    int restartFirmware();
    int patchSemCtrl(bool acquire, uint8_t &semStatus);
    int initDownload(uint32_t addr, uint32_t len, uint32_t mode);
    int sendFirmwareScatter(const uint8_t *data, size_t len, size_t maxChunk = 4096);
    int startPatch(uint8_t &status);
    int startFirmware(uint32_t overrideAddr, uint32_t option);

    // Hardware & MAC configuration
    int getNicCapability(NicCapability &cap);
    int setEepromEfuseMode();
    int setMacEnable(uint8_t band, bool enable);
    int setChannelInfo(const ChannelConfig &ch, uint8_t antennaMask, uint32_t cmd);
    int setChannel(const ChannelConfig &ch, uint8_t antennaMask);
    int setRxFilter(uint32_t bitOp, uint32_t bitMap);
    int setChannelDomain(const char alpha2[2], const ScanChannelEntry *channels, size_t count);
    int setDeepSleep(bool enable);
    int setHifSuspend(bool suspend);

    // Device / BSS / STA / Security / Scan
    int uniAddDev(uint8_t omacIdx, uint8_t bssIdx, uint16_t bmcWcid,
                  const uint8_t macAddr[6], bool enable);
    int uniAddBss(uint8_t omacIdx, uint8_t bssIdx, uint16_t bmcWcid,
                  const uint8_t bssid[6], const ChannelConfig &ch,
                  uint16_t bcnInterval, uint8_t dtimPeriod, bool qos, bool enable);
    int uniUpdateSta(const StaUpdateParams &p);
    int uniSetKey(const KeyInstallParams &p);
    int setBssConnected(uint8_t bssIdx, uint16_t aid, uint16_t bcnInterval,
                        uint8_t dtimPeriod, bool enable);
    int startHwScan(const ScanRequestParams &params);
    int cancelHwScan(uint8_t seqNum);

    // Static parser helpers
    static int parseNicCapabilityPayload(const uint8_t *payload, size_t len, NicCapability &cap);
    static int parseResponseHeader(uint32_t cmd, uint8_t expectedSeq,
                                   const uint8_t *pkt, size_t len,
                                   size_t &payloadOffset);

private:
    uint8_t nextSeq();

    IMT7902Bus &mBus;
    MT7902Dma &mDma;
    IMcuEventObserver *mObserver = nullptr;
    uint8_t mMsgSeq = 0;
    uint8_t mScanSeq = 0;

    // Synchronous response slot (single in-flight MCU command at a time)
    bool mWaitingResp = false;
    uint8_t mWaitSeq = 0;
    uint32_t mWaitCmd = 0;
    bool mRespReceived = false;
    uint8_t mRespStorage[2048] = {};
    size_t mRespSize = 0;
};

} // namespace mt7902

#endif // MT7902_MCU_HPP
