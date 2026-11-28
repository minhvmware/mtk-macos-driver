// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902Mac.hpp"

namespace mt7902 {

MT7902Mac::MT7902Mac(IMT7902Bus &bus, MT7902Dma &dma)
    : mBus(bus), mDma(dma) {
    mTokens = static_cast<TxTokenSlot *>(kallocZero(sizeof(TxTokenSlot) * MT7902_TOKEN_SIZE));
}

MT7902Mac::~MT7902Mac() {
    releaseAllTokens();
    if (mTokens) {
        kfreeSized(mTokens, sizeof(TxTokenSlot) * MT7902_TOKEN_SIZE);
        mTokens = nullptr;
    }
}

bool MT7902Mac::wtblUpdate(uint16_t idx, uint32_t mask) {
    mBus.rmw(MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
             fieldPrep(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);
    return mBus.pollMs(MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 10, 0);
}

void MT7902Mac::initBand(uint8_t band) {
    mBus.rmw(MT_TMAC_CTCR0(band), MT_TMAC_CTCR0_INS_DDLMT_REFTIME,
             fieldPrep(MT_TMAC_CTCR0_INS_DDLMT_REFTIME, 0x3f));
    mBus.set(MT_TMAC_CTCR0(band),
             MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN | MT_TMAC_CTCR0_INS_DDLMT_EN);

    mBus.set(MT_WF_RMAC_MIB_TIME0(band), MT_WF_RMAC_MIB_RXTIME_EN);
    mBus.set(MT_WF_RMAC_MIB_AIRTIME0(band), MT_WF_RMAC_MIB_RXTIME_EN);
    mBus.set(MT_MIB_SCR1(band), MT_MIB_TXDUR_EN | MT_MIB_RXDUR_EN);

    mBus.rmw(MT_DMA_DCR0(band), MT_DMA_DCR0_MAX_RX_LEN,
             fieldPrep(MT_DMA_DCR0_MAX_RX_LEN, 1536));
    mBus.clear(MT_DMA_DCR0(band), MT_DMA_DCR0_RXD_G5_EN);
}

void MT7902Mac::setTiming(bool is2Ghz, uint8_t slotTime) {
    // Wire-exact match to Linux mt792x_mac_set_timeing():
    // 2.4 GHz requires SIFS = 10 us; 5/6 GHz requires SIFS = 16 us for hardware ACK/BlockAck timing!
    uint32_t cck = fieldPrep(MT_TIMEOUT_VAL_PLCP, 231U) | fieldPrep(MT_TIMEOUT_VAL_CCA, 48U);
    uint32_t ofdm = fieldPrep(MT_TIMEOUT_VAL_PLCP, 60U) | fieldPrep(MT_TIMEOUT_VAL_CCA, 28U);
    uint32_t sifs = is2Ghz ? 10U : 16U;

    mBus.set(MT_ARB_SCR(0), MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
    mBus.delayUs(1);

    mBus.wr(MT_TMAC_CDTR(0), cck);
    mBus.wr(MT_TMAC_ODTR(0), ofdm);
    mBus.wr(MT_TMAC_ICR0(0),
            fieldPrep(MT_IFS_EIFS, 360U) |
            fieldPrep(MT_IFS_RIFS, 2U) |
            fieldPrep(MT_IFS_SIFS, sifs) |
            fieldPrep(MT_IFS_SLOT, slotTime));

    uint32_t cfend = (slotTime < 20 || !is2Ghz)
                         ? MT792x_CFEND_RATE_DEFAULT
                         : MT792x_CFEND_RATE_11B;
    mBus.rmw(MT_AGG_ACR0(0), MT_AGG_ACR_CFEND_RATE, fieldPrep(MT_AGG_ACR_CFEND_RATE, cfend));
    mBus.clear(MT_ARB_SCR(0), MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
}

int MT7902Mac::initHardware() {
    // Match Linux mt7921_mac_init()
    mBus.rmw(MT_MDP_DCR1, MT_MDP_DCR1_MAX_RX_LEN,
             fieldPrep(MT_MDP_DCR1_MAX_RX_LEN, 1536));
    mBus.set(MT_MDP_DCR0, MT_MDP_DCR0_DAMSDU_EN);
    mBus.set(MT_MDP_DCR0, MT_MDP_DCR0_RX_HDR_TRANS_EN);

    for (uint16_t i = 0; i < MT7902_WTBL_SIZE; ++i)
        wtblUpdate(i, MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

    initBand(0);
    initBand(1);
    return kOk;
}

int MT7902Mac::allocToken(uint16_t wcid, void *osPacket, const DmaBuffer &dmaBuf) {
    if (!mTokens || mActiveTokens >= MT7902_TOKEN_SIZE)
        return kErrNoSpace;

    for (uint32_t i = 0; i < MT7902_TOKEN_SIZE; ++i) {
        uint16_t id = static_cast<uint16_t>((mNextTokenCursor + i) % MT7902_TOKEN_SIZE);
        if (!mTokens[id].inUse) {
            if (dmaBuf.virt && mTokens[id].pooledBuf && mTokens[id].dmaBuf.virt) {
                mBus.freeDma(mTokens[id].dmaBuf);
                mTokens[id].pooledBuf = false;
            }
            mTokens[id].inUse = true;
            mTokens[id].wcid = wcid;
            mTokens[id].osPacket = osPacket;
            if (dmaBuf.virt) {
                mTokens[id].dmaBuf = dmaBuf;
                mTokens[id].pooledBuf = false;
            }
            mActiveTokens++;
            mNextTokenCursor = static_cast<uint16_t>((id + 1U) % MT7902_TOKEN_SIZE);
            return id;
        }
    }
    return kErrNoSpace;
}

bool MT7902Mac::releaseToken(uint16_t tokenId, TxTokenSlot &outSlot) {
    if (!mTokens || tokenId >= MT7902_TOKEN_SIZE || !mTokens[tokenId].inUse)
        return false;
    outSlot = mTokens[tokenId];
    if (mTokens[tokenId].pooledBuf) {
        // Keep persistent pooled DMA buffer attached to this slot for zero-alloc reuse
        outSlot.dmaBuf = {};
        mTokens[tokenId].inUse = false;
        mTokens[tokenId].wcid = 0;
        mTokens[tokenId].osPacket = nullptr;
    } else {
        mTokens[tokenId] = {};
    }
    if (mActiveTokens > 0)
        mActiveTokens--;
    return true;
}

void MT7902Mac::releaseAllTokens(void (*onFree)(void *osPacket, void *ctx), void *ctx) {
    if (!mTokens)
        return;
    for (uint32_t i = 0; i < MT7902_TOKEN_SIZE; ++i) {
        if (mTokens[i].dmaBuf.virt)
            mBus.freeDma(mTokens[i].dmaBuf);
        if (mTokens[i].inUse && mTokens[i].osPacket && onFree)
            onFree(mTokens[i].osPacket, ctx);
        mTokens[i] = {};
    }
    mActiveTokens = 0;
}

static uint8_t ieee80211HdrLen(const uint8_t *frame, size_t len) {
    if (len < 24)
        return 24;
    uint16_t fc = getLe16(frame);
    uint8_t type = (fc >> 2) & 0x3U;
    uint8_t stype = (fc >> 4) & 0xfU;
    uint8_t hlen = 24;
    if (type == 2) { // Data
        if ((fc & 0x0300U) == 0x0300U) // ToDS + FromDS
            hlen = 30;
        if (stype & 0x08U) // QoS
            hlen += 2;
    }
    return hlen;
}

void MT7902Mac::buildTxwi(Mt7902Txwi &out, const uint8_t *frame, size_t frameLen,
                          uint64_t payloadPhys, uint16_t tokenId,
                          const TxPacketOptions &opts) {
    memset(&out, 0, sizeof(out));

    // LMAC queue index: reversed AC order (3 - ac) for data, ALTX0 for mgmt/fixed-rate
    uint8_t qIdx = opts.is8023
                       ? static_cast<uint8_t>(opts.wmmIdx * 4U + (3U - (opts.ac & 3U)))
                       : static_cast<uint8_t>(MT_LMAC_ALTX0);

    uint32_t dw0 = fieldPrep(MT_TXD0_TX_BYTES, static_cast<uint32_t>(frameLen + MT_TXD_SIZE)) |
                   fieldPrep(MT_TXD0_PKT_FMT, MT_TX_TYPE_CT) |
                   fieldPrep(MT_TXD0_Q_IDX, qIdx);

    uint32_t dw1 = MT_TXD1_LONG_FORMAT |
                   fieldPrep(MT_TXD1_WLAN_IDX, opts.wcid) |
                   fieldPrep(MT_TXD1_OWN_MAC, opts.omacIdx);
    if (opts.bandIdx)
        dw1 |= MT_TXD1_TGID;

    uint32_t dw2 = 0;
    uint32_t dw3 = fieldPrep(MT_TXD3_REM_TX_COUNT, 15);
    if (opts.protect)
        dw3 |= MT_TXD3_PROTECT_FRAME;
    if (opts.noAck)
        dw3 |= MT_TXD3_NO_ACK;

    uint32_t dw5 = fieldPrep(MT_TXD5_PID, opts.pid);
    if (opts.pid >= 1)
        dw5 |= MT_TXD5_TX_STATUS_HOST;

    uint32_t dw6 = 0;
    uint32_t dw7 = 0;

    if (opts.is8023) {
        dw1 |= fieldPrep(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_3) |
               fieldPrep(MT_TXD1_TID, opts.tid & 0x7U);
        if (frameLen >= 14) {
            uint16_t ethType = getBe16(frame + 12);
            if (ethType >= 0x0600U)
                dw1 |= MT_TXD1_ETH_802_3;
        }
        uint8_t fcType = 2; // IEEE80211_FTYPE_DATA >> 2
        uint8_t fcStype = opts.qos ? 8 : 0; // QoS Data vs Data
        dw2 |= fieldPrep(MT_TXD2_FRAME_TYPE, fcType) |
               fieldPrep(MT_TXD2_SUB_TYPE, fcStype);
        dw7 |= fieldPrep(MT_TXD7_TYPE, fcType) |
               fieldPrep(MT_TXD7_SUB_TYPE, fcStype);
        if (opts.hwAmsdu && opts.qos && !opts.fixedRate && opts.pid == 0)
            dw7 |= MT_TXD7_HW_AMSDU;
    } else {
        uint16_t fc = (frameLen >= 2) ? getLe16(frame) : 0;
        uint8_t fcType = (fc >> 2) & 0x3U;
        uint8_t fcStype = (fc >> 4) & 0xfU;
        uint8_t hlen = ieee80211HdrLen(frame, frameLen);

        dw1 |= fieldPrep(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
               fieldPrep(MT_TXD1_HDR_INFO, hlen / 2U) |
               fieldPrep(MT_TXD1_TID, opts.tid & 0x7U);
        dw2 |= fieldPrep(MT_TXD2_FRAME_TYPE, fcType) |
               fieldPrep(MT_TXD2_SUB_TYPE, fcStype);
        if (opts.multicast)
            dw2 |= MT_TXD2_MULTICAST;
        if (fcType != 2 || opts.multicast || opts.fixedRate) {
            dw2 |= MT_TXD2_FIX_RATE | MT_TXD2_HTC_VLD;
            dw3 |= MT_TXD3_BA_DISABLE;
            dw6 |= MT_TXD6_FIXED_BW | fieldPrep(MT_TXD6_TX_RATE, opts.fixedRateVal);
        }
        dw7 |= fieldPrep(MT_TXD7_TYPE, fcType) |
               fieldPrep(MT_TXD7_SUB_TYPE, fcStype);
    }

    putLe32(&out.txd[0], dw0);
    putLe32(&out.txd[1], dw1);
    putLe32(&out.txd[2], dw2);
    putLe32(&out.txd[3], dw3);
    putLe32(&out.txd[4], 0);
    putLe32(&out.txd[5], dw5);
    putLe32(&out.txd[6], dw6);
    putLe32(&out.txd[7], dw7);

    // Hardware TXP: token ID + single contiguous DMA payload pointer
    // Connac2 (MT7902/MT7921) uses ONLY MT_TXD_LEN_LAST (BIT(15)), never MT_TXD_LEN_MSDU_LAST!
    putLe16(&out.txp.msdu_id[0], static_cast<uint16_t>(tokenId | MT_MSDU_ID_VALID));
    putLe32(&out.txp.ptr[0].buf0, static_cast<uint32_t>(payloadPhys & 0xffffffffU));
    uint16_t len0 = static_cast<uint16_t>((frameLen & MT_TXD_LEN_MASK) | MT_TXD_LEN_LAST);
    putLe16(&out.txp.ptr[0].len0, len0);
}

int MT7902Mac::transmitFrame(const uint8_t *frame, size_t frameLen, const TxPacketOptions &opts,
                             bool kick) {
    if (!frame || frameLen < kEthHdrLen || frameLen > 2048)
        return kErrInvalid;

    DmaBuffer emptyBuf;
    int tokenId = allocToken(opts.wcid, nullptr, emptyBuf);
    if (tokenId < 0)
        return tokenId;

    TxTokenSlot &slotRef = mTokens[tokenId];
    if (!slotRef.dmaBuf.virt) {
        if (!mBus.allocDma(2048, 64, slotRef.dmaBuf)) {
            TxTokenSlot dummy;
            releaseToken(static_cast<uint16_t>(tokenId), dummy);
            return kErrNoMem;
        }
        slotRef.pooledBuf = true;
    }

    memcpy(slotRef.dmaBuf.virt, frame, frameLen);
    if (kick)
        mBus.syncDmaForDevice(slotRef.dmaBuf);

    Mt7902Txwi txwi;
    buildTxwi(txwi, frame, frameLen, slotRef.dmaBuf.phys,
              static_cast<uint16_t>(tokenId), opts);

    // Submit 64-byte TXWI in desc.buf0 (hardware fetches payload via TXP.ptr[0].buf0)
    int err = mDma.txSubmitCopy(kTxBand0, &txwi, sizeof(txwi), kick);
    if (err != kOk) {
        TxTokenSlot slot;
        if (releaseToken(static_cast<uint16_t>(tokenId), slot) && slot.dmaBuf.virt)
            mBus.freeDma(slot.dmaBuf);
        return err;
    }

    mStats.txPackets++;
    mStats.txBytes += frameLen;
    return kOk;
}

uint32_t MT7902Mac::handleTxFree(const uint8_t *data, size_t len,
                                 void (*onPacketDone)(void *osPacket, bool ok, void *ctx),
                                 void *ctx) {
    if (!data || len < sizeof(Mt76ConnacTxFree))
        return 0;

    mDma.txCleanup(kTxBand0);

    const auto *hdr = reinterpret_cast<const Mt76ConnacTxFree *>(data);
    uint16_t ctrl = getLe16(&hdr->ctrl);
    uint32_t count = fieldGet(MT_TX_FREE_MSDU_CNT, ctrl);
    const uint8_t *ptr = data + sizeof(Mt76ConnacTxFree);
    const uint8_t *end = data + len;

    uint32_t released = 0;
    for (uint32_t i = 0; i < count && ptr + 4 <= end; ++i, ptr += 4) {
        uint32_t info = getLe32(ptr);
        if (info & MT_TX_FREE_PAIR) {
            // Pair marker precedes entries for a WCID; increment count matching mt7921_mac_tx_free
            count++;
            continue;
        }
        uint16_t msduId = static_cast<uint16_t>(fieldGet(MT_TX_FREE_MSDU_ID, info));
        uint8_t stat = static_cast<uint8_t>(fieldGet(MT_TX_FREE_STATUS, info));
        uint32_t txCnt = fieldGet(MT_TX_FREE_COUNT, info);
        if (txCnt > 1)
            mStats.txRetries += (txCnt - 1U);
        if (stat != 0) {
            mStats.txFailed++;
            if (mStats.txFailed <= 16)
                MTK_LOG("TX free failed: status=%u retries=%u token=%u", stat, txCnt, msduId);
        }

        TxTokenSlot slot;
        if (releaseToken(msduId, slot)) {
            if (slot.dmaBuf.virt)
                mBus.freeDma(slot.dmaBuf);
            if (onPacketDone)
                onPacketDone(slot.osPacket, stat == 0, ctx);
            released++;
        }
    }
    return released;
}

bool MT7902Mac::parseRxDescriptor(const uint8_t *raw, size_t rawLen,
                                  uint8_t antennaMask, ParsedRxFrame &out) {
    out = {};
    if (!raw || rawLen < 24)
        return false;

    uint32_t rxd0 = getLe32(raw + 0);
    uint32_t rxd1 = getLe32(raw + 4);
    uint32_t rxd2 = getLe32(raw + 8);
    uint32_t rxd3 = getLe32(raw + 12);
    uint32_t rxd4 = getLe32(raw + 16);

    uint8_t pktType = static_cast<uint8_t>(fieldGet(MT_RXD0_PKT_TYPE, rxd0));
    uint8_t pktFlag = static_cast<uint8_t>(fieldGet(MT_RXD0_PKT_FLAG, rxd0));
    if (pktType == PKT_TYPE_RX_EVENT && pktFlag == 0x1)
        pktType = PKT_TYPE_NORMAL_MCU;
    if (pktType != PKT_TYPE_NORMAL && pktType != PKT_TYPE_NORMAL_MCU)
        return false;

    if (rxd1 & MT_RXD1_NORMAL_BAND_IDX)
        return false;
    if (rxd2 & (MT_RXD2_NORMAL_AMSDU_ERR | MT_RXD2_NORMAL_MAX_LEN_ERROR))
        return false;

    out.is8023 = (rxd2 & MT_RXD2_NORMAL_HDR_TRANS) != 0;
    if (out.is8023 && (rxd1 & MT_RXD1_NORMAL_CM))
        return false;

    out.wlanIdx = static_cast<uint16_t>(fieldGet(MT_RXD1_NORMAL_WLAN_IDX, rxd1));
    out.chFreq = static_cast<uint8_t>(fieldGet(MT_RXD3_NORMAL_CH_FREQ, rxd3));
    out.unicast = fieldGet(MT_RXD3_NORMAL_ADDR_TYPE, rxd3) == MT_RXD3_NORMAL_U2M;
    out.fcsError = (rxd1 & MT_RXD1_NORMAL_FCS_ERR) != 0;
    out.micError = (rxd1 & MT_RXD1_NORMAL_TKIP_MIC_ERR) != 0;
    out.icvError = (rxd1 & MT_RXD1_NORMAL_ICV_ERR) != 0;
    out.decrypted = fieldGet(MT_RXD1_NORMAL_SEC_MODE, rxd1) != 0 &&
                    (rxd1 & (MT_RXD1_NORMAL_CLM | MT_RXD1_NORMAL_CM)) == 0;
    uint32_t amsduInfo = fieldGet(MT_RXD4_NORMAL_PAYLOAD_FORMAT, rxd4);
    out.amsdu = (amsduInfo != 0);
    if (out.amsdu) {
        out.firstAmsdu = (amsduInfo == MT_RXD4_FIRST_AMSDU_FRAME);
        out.lastAmsdu = (amsduInfo == MT_RXD4_LAST_AMSDU_FRAME);
    }
    out.tid = static_cast<uint8_t>(fieldGet(MT_RXD2_NORMAL_TID, rxd2));

    uint32_t removePad = fieldGet(MT_RXD2_NORMAL_HDR_OFFSET, rxd2);
    size_t pos = 24; // 6 dwords base RXD

    // Group 4 (MAC header info, 4 dwords: matches Linux mt7921_mac_fill_rx)
    if (rxd1 & MT_RXD1_NORMAL_GROUP_4) {
        if (pos + 16 >= rawLen) return false;
        uint32_t v0 = getLe32(raw + pos + 0);
        uint32_t v2 = getLe32(raw + pos + 8);
        out.hasGroup4 = true;
        out.frameCtrl = static_cast<uint16_t>(fieldGet(MT_RXD6_FRAME_CONTROL, v0));
        uint16_t seqCtrl = static_cast<uint16_t>(fieldGet(MT_RXD8_SEQ_CTRL, v2));
        out.qosCtrl = static_cast<uint16_t>(fieldGet(MT_RXD8_QOS_CTL, v2));
        out.seqNo = static_cast<uint16_t>((seqCtrl >> 4) & 0x0fffU);
        out.tid = static_cast<uint8_t>(out.qosCtrl & 0x0fU);
        pos += 16;
    }

    // Group 1 (IV/PN, 4 dwords)
    if (rxd1 & MT_RXD1_NORMAL_GROUP_1) {
        if (pos + 16 >= rawLen) return false;
        pos += 16;
    }

    // Group 2 (Timestamp, 2 dwords)
    if (rxd1 & MT_RXD1_NORMAL_GROUP_2) {
        if (pos + 8 >= rawLen) return false;
        out.timestamp = getLe32(raw + pos);
        pos += 8;
    }

    // Group 3 (P-RXV, 2 dwords) + optional Group 5 (C-RXV, 18 dwords)
    if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {
        if (pos + 8 >= rawLen) return false;
        uint32_t v0 = getLe32(raw + pos);
        uint32_t v1 = getLe32(raw + pos + 4);
        pos += 8;

        out.phyMode = static_cast<uint8_t>(fieldGet(MT_PRXV_TX_MODE, v0));
        uint8_t rawRate = static_cast<uint8_t>(fieldGet(MT_PRXV_TX_RATE, v0));
        out.nss = static_cast<uint8_t>(fieldGet(MT_PRXV_NSTS, v0) + 1U);
        if (fieldGet(MT_PRXV_HT_STBC, v0) && out.nss > 1)
            out.nss = static_cast<uint8_t>(out.nss >> 1);
        out.bw = static_cast<uint8_t>(fieldGet(MT_PRXV_FRAME_MODE, v0));
        out.gi = static_cast<uint8_t>(fieldGet(MT_PRXV_HT_SGI, v0));

        // Validate P-RXV mode/rate/bw matching Linux mt76_connac2_mac_fill_rx_rate()
        // so A-MSDU subframe fill patterns (e.g. 0x55555555) never corrupt link MCS/rate.
        bool modeRateOk = false;
        if (out.bw <= 3U) { // 0=20, 1=40, 2=80, 3=160 MHz
            if (out.phyMode == MT_PHY_TYPE_OFDM) {
                switch (rawRate & 0x0fU) {
                case 0x0b: out.mcs = 0; modeRateOk = true; break; // 6 Mbps
                case 0x0f: out.mcs = 1; modeRateOk = true; break; // 9 Mbps
                case 0x0a: out.mcs = 2; modeRateOk = true; break; // 12 Mbps
                case 0x0e: out.mcs = 3; modeRateOk = true; break; // 18 Mbps
                case 0x09: out.mcs = 4; modeRateOk = true; break; // 24 Mbps
                case 0x0d: out.mcs = 5; modeRateOk = true; break; // 36 Mbps
                case 0x08: out.mcs = 6; modeRateOk = true; break; // 48 Mbps
                case 0x0c: out.mcs = 7; modeRateOk = true; break; // 54 Mbps
                default: break;
                }
            } else if (out.phyMode == MT_PHY_TYPE_CCK) {
                if (rawRate <= 11) {
                    out.mcs = static_cast<uint8_t>(rawRate & 0x03U);
                    modeRateOk = true;
                }
            } else if (out.phyMode == MT_PHY_TYPE_HT || out.phyMode == MT_PHY_TYPE_HT_GF) {
                if (rawRate <= 31) {
                    out.mcs = rawRate;
                    modeRateOk = true;
                }
            } else if (out.phyMode == MT_PHY_TYPE_VHT) {
                if (rawRate <= 11) {
                    out.mcs = rawRate;
                    modeRateOk = true;
                }
            } else if (out.phyMode >= MT_PHY_TYPE_HE_SU && out.phyMode <= MT_PHY_TYPE_HE_MU) {
                // Strip Connac2 DCM (BIT(4)) and ER_SU_106T (BIT(5)) bits
                uint8_t heMcs = static_cast<uint8_t>(rawRate & 0x0fU);
                if (heMcs <= 11) {
                    out.mcs = heMcs;
                    modeRateOk = true;
                }
            }
        }
        out.rateValid = modeRateOk;

        if (rxd1 & MT_RXD1_NORMAL_GROUP_5) {
            if (pos + 72 >= rawLen) return false;
            v1 = getLe32(raw + pos + 24);
            pos += 72;
        }

        out.chainRssi[0] = rcpiToRssi(fieldGet(MT_PRXV_RCPI0, v1));
        out.chainRssi[1] = rcpiToRssi(fieldGet(MT_PRXV_RCPI1, v1));
        out.chainRssi[2] = rcpiToRssi(fieldGet(MT_PRXV_RCPI2, v1));
        out.chainRssi[3] = rcpiToRssi(fieldGet(MT_PRXV_RCPI3, v1));

        uint8_t mask = antennaMask ? antennaMask : 0x1;
        int8_t best = -128;
        for (int c = 0; c < 4; ++c) {
            if ((mask & (1U << c)) && out.chainRssi[c] < 0 && out.chainRssi[c] > best)
                best = out.chainRssi[c];
        }
        out.rssi = (best > -128) ? best : -70;
    }

    size_t hdrGap = pos + 2U * removePad;
    if (hdrGap >= rawLen)
        return false;

    out.payloadOffset = static_cast<uint32_t>(hdrGap);
    out.payloadLen = static_cast<uint32_t>(rawLen - hdrGap);

    // Match Linux mt7921_mac_fill_rx() aggregation eligibility:
    // unicast QoS Data (excluding QoS Null and No-Ack policy) with valid Group 4 seq/qos info.
    if (out.is8023 && out.hasGroup4 && out.unicast) {
        uint8_t fcType = static_cast<uint8_t>((out.frameCtrl >> 2) & 0x3U);
        uint8_t fcStype = static_cast<uint8_t>((out.frameCtrl >> 4) & 0xfU);
        bool isQosData = (fcType == 2U) && ((fcStype & 0x8U) != 0U);
        bool isQosNull = (fcType == 2U) && (fcStype == 0xcU);
        uint8_t ackPolicy = static_cast<uint8_t>((out.qosCtrl >> 5) & 0x3U);
        out.aggr = isQosData && !isQosNull && (ackPolicy != 1U /* NOACK */);
    }

    out.valid = true;
    return true;
}

uint32_t MT7902Mac::estimateRateMbps(uint8_t phyMode, uint8_t mcs, uint8_t nss, uint8_t bw) {
    if (nss == 0) nss = 1;
    uint32_t bwMult = (bw == 0) ? 1U : (bw == 1 ? 2U : (bw == 2 ? 4U : 8U));
    switch (phyMode) {
    case MT_PHY_TYPE_CCK:
        return (mcs <= 3) ? (mcs == 0 ? 1 : (mcs == 1 ? 2 : (mcs == 2 ? 5 : 11))) : 11;
    case MT_PHY_TYPE_OFDM: {
        static constexpr uint32_t kOfdm[] = { 6, 9, 12, 18, 24, 36, 48, 54 };
        return kOfdm[mcs & 7U];
    }
    case MT_PHY_TYPE_HT:
    case MT_PHY_TYPE_HT_GF:
        return ((mcs & 7U) + 1U) * 18U * bwMult * nss / 2U;
    case MT_PHY_TYPE_VHT:
        return ((mcs & 0xfU) + 1U) * 19U * bwMult * nss / 2U;
    case MT_PHY_TYPE_HE_SU:
    case MT_PHY_TYPE_HE_EXT_SU:
    case MT_PHY_TYPE_HE_TB:
    case MT_PHY_TYPE_HE_MU:
        return ((mcs & 0xfU) + 1U) * 24U * bwMult * nss / 2U;
    default:
        return 54;
    }
}

} // namespace mt7902
