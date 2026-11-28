// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Connac2 MAC init, TXWI/HW-TXP encoder, TX token manager, and RXD/P-RXV decoder

#ifndef MT7902_MAC_HPP
#define MT7902_MAC_HPP

#include "../../include/MT7902Common.hpp"
#include "MT7902Bus.hpp"
#include "MT7902Dma.hpp"
#include "MT7902DmaTypes.hpp"
#include "MT7902Regs.hpp"

namespace mt7902 {

struct TxTokenSlot {
    bool inUse = false;
    bool pooledBuf = false;
    uint16_t wcid = 0;
    void *osPacket = nullptr;
    DmaBuffer dmaBuf; // Owned scratch DMA buffer for copy-path TX (reused when pooledBuf == true)
};

struct TxPacketOptions {
    uint16_t wcid = 0;
    uint8_t omacIdx = 0;
    uint8_t wmmIdx = 0;
    uint8_t bandIdx = 0;
    uint8_t tid = 0;
    uint8_t ac = 0;           // 0=VO, 1=VI, 2=BE, 3=BK (mac80211 order)
    bool is8023 = true;       // true: 802.3 HW encap; false: raw 802.11 mgmt/ctrl/data
    bool qos = true;
    bool protect = false;
    bool noAck = false;
    bool multicast = false;
    bool hwAmsdu = false;
    bool fixedRate = false;
    uint16_t fixedRateVal = 0;
    uint8_t pid = 0;
};

struct ParsedRxFrame {
    bool valid = false;
    bool rateValid = false;
    bool hasGroup4 = false;
    bool is8023 = false;      // HW header-translated Ethernet frame
    bool decrypted = false;
    bool fcsError = false;
    bool micError = false;
    bool icvError = false;
    bool amsdu = false;
    bool firstAmsdu = false;
    bool lastAmsdu = false;
    bool aggr = false;        // Eligible for IEEE 802.11 RX A-MPDU BA reorder
    bool unicast = false;
    uint16_t wlanIdx = 0;
    uint16_t frameCtrl = 0;
    uint16_t qosCtrl = 0;
    uint8_t chFreq = 0;
    uint8_t tid = 0;
    uint16_t seqNo = 0;
    int8_t rssi = -100;
    int8_t chainRssi[4] = { -100, -100, -100, -100 };
    uint8_t phyMode = MT_PHY_TYPE_OFDM;
    uint8_t mcs = 0;
    uint8_t nss = 1;
    uint8_t bw = 0;           // 0=20, 1=40, 2=80, 3=160
    uint8_t gi = 0;
    uint32_t timestamp = 0;
    uint32_t payloadOffset = 0;
    uint32_t payloadLen = 0;
};

struct MacStats {
    uint64_t txPackets = 0;
    uint64_t txBytes = 0;
    uint64_t txRetries = 0;
    uint64_t txFailed = 0;
    uint64_t rxPackets = 0;
    uint64_t rxBytes = 0;
    uint64_t rxFcsErrors = 0;
};

class MT7902Mac {
public:
    MT7902Mac(IMT7902Bus &bus, MT7902Dma &dma);
    ~MT7902Mac();

    int initHardware();
    void initBand(uint8_t band);
    void setTiming(bool is2Ghz, uint8_t slotTime = 9);
    bool wtblUpdate(uint16_t idx, uint32_t mask);

    // TX token management (MT7902 HW cut-through tokens)
    int allocToken(uint16_t wcid, void *osPacket, const DmaBuffer &dmaBuf);
    bool releaseToken(uint16_t tokenId, TxTokenSlot &outSlot);
    void releaseAllTokens(void (*onFree)(void *osPacket, void *ctx) = nullptr, void *ctx = nullptr);
    uint32_t activeTokens() const { return mActiveTokens; }

    // Wire-format TXWI (32B TXD + 32B HW TXP) builders (pure static, unit-testable)
    static void buildTxwi(Mt7902Txwi &out, const uint8_t *frame, size_t frameLen,
                          uint64_t payloadPhys, uint16_t tokenId,
                          const TxPacketOptions &opts);

    // Wire-format RXD parser (handles DW0..DW4 + Groups 1/2/3/4/5 + remove_pad)
    static bool parseRxDescriptor(const uint8_t *raw, size_t rawLen,
                                  uint8_t antennaMask, ParsedRxFrame &out);

    // Transmit a frame through WFDMA0 TX Ring 0 (set kick=false when batching, then call dma.txKick)
    int transmitFrame(const uint8_t *frame, size_t frameLen, const TxPacketOptions &opts,
                      bool kick = true);

    // Handle PKT_TYPE_TXRX_NOTIFY from RX Ring 0 (frees tokens & DMA buffers)
    uint32_t handleTxFree(const uint8_t *data, size_t len,
                          void (*onPacketDone)(void *osPacket, bool ok, void *ctx) = nullptr,
                          void *ctx = nullptr);

    // Compute approximate link speed in Mbps from RX/TX PHY parameters
    static uint32_t estimateRateMbps(uint8_t phyMode, uint8_t mcs, uint8_t nss, uint8_t bw);

    const MacStats &stats() const { return mStats; }
    void recordRxBytes(size_t bytes) { mStats.rxPackets++; mStats.rxBytes += bytes; }

private:
    IMT7902Bus &mBus;
    MT7902Dma &mDma;
    TxTokenSlot *mTokens = nullptr;
    uint32_t mActiveTokens = 0;
    uint16_t mNextTokenCursor = 0;
    MacStats mStats;
};

} // namespace mt7902

#endif // MT7902_MAC_HPP
