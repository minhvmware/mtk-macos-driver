// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902Mcu.hpp"

namespace mt7902 {

MT7902Mcu::MT7902Mcu(IMT7902Bus &bus, MT7902Dma &dma)
    : mBus(bus), mDma(dma) {}

uint8_t MT7902Mcu::nextSeq() {
    mMsgSeq = static_cast<uint8_t>((mMsgSeq + 1U) & 0x0fU);
    if (mMsgSeq == 0)
        mMsgSeq = static_cast<uint8_t>((mMsgSeq + 1U) & 0x0fU);
    return mMsgSeq;
}

size_t MT7902Mcu::buildMessage(uint32_t cmd, const void *payload, size_t payloadLen,
                               uint8_t *outBuf, size_t outCap, uint8_t &outSeq) {
    uint8_t seq = nextSeq();
    outSeq = seq;

    if (cmd == MCU_CMD_FW_SCATTER) {
        if (payloadLen > outCap)
            return 0;
        if (payload && payloadLen > 0)
            memcpy(outBuf, payload, payloadLen);
        return payloadLen;
    }

    bool isUni = (cmd & MCU_CMD_FIELD_UNI) != 0;
    size_t hdrLen = isUni ? sizeof(Mt76Connac2McuUniTxd) : sizeof(Mt76Connac2McuTxd);
    size_t totalLen = hdrLen + payloadLen;
    if (totalLen > outCap)
        return 0;

    memset(outBuf, 0, hdrLen);
    if (payload && payloadLen > 0)
        memcpy(outBuf + hdrLen, payload, payloadLen);

    uint32_t txd0 = fieldPrep(MT_TXD0_TX_BYTES, static_cast<uint32_t>(totalLen)) |
                    fieldPrep(MT_TXD0_PKT_FMT, MT_TX_TYPE_CMD) |
                    fieldPrep(MT_TXD0_Q_IDX, MT_TX_MCU_PORT_RX_Q0);
    uint32_t txd1 = MT_TXD1_LONG_FORMAT |
                    fieldPrep(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_CMD);

    uint8_t mcuCmd = static_cast<uint8_t>(fieldGet(MCU_CMD_FIELD_ID, cmd));

    if (isUni) {
        auto *u = reinterpret_cast<Mt76Connac2McuUniTxd *>(outBuf);
        putLe32(&u->txd[0], txd0);
        putLe32(&u->txd[1], txd1);
        putLe16(&u->len, static_cast<uint16_t>(totalLen - sizeof(u->txd)));
        putLe16(&u->cid, mcuCmd);
        u->pkt_type = MCU_PKT_ID;
        u->seq = seq;
        u->s2d_index = MCU_S2D_H2N;
        u->option = MCU_CMD_UNI_EXT_ACK;
    } else {
        auto *m = reinterpret_cast<Mt76Connac2McuTxd *>(outBuf);
        putLe32(&m->txd[0], txd0);
        putLe32(&m->txd[1], txd1);
        putLe16(&m->len, static_cast<uint16_t>(totalLen - sizeof(m->txd)));
        putLe16(&m->pq_id, mcuPqId(MT_TX_PORT_IDX_MCU, MT_TX_MCU_PORT_RX_Q0));
        m->cid = mcuCmd;
        m->pkt_type = MCU_PKT_ID;
        m->seq = seq;
        m->ext_cid = static_cast<uint8_t>(fieldGet(MCU_CMD_FIELD_EXT_ID, cmd));

        if (m->ext_cid || (cmd & MCU_CMD_FIELD_CE)) {
            m->set_query = (cmd & MCU_CMD_FIELD_QUERY) ? MCU_Q_QUERY : MCU_Q_SET;
            m->ext_cid_ack = m->ext_cid ? 1 : 0;
        } else {
            m->set_query = MCU_Q_NA;
        }
        m->s2d_index = (cmd & MCU_CMD_FIELD_WA) ? MCU_S2D_H2C : MCU_S2D_H2N;
    }

    return totalLen;
}

int MT7902Mcu::parseResponseHeader(uint32_t cmd, uint8_t expectedSeq,
                                   const uint8_t *pkt, size_t len,
                                   size_t &payloadOffset) {
    if (!pkt || len < sizeof(Mt76Connac2McuRxd))
        return kErrInvalid;

    const auto *rxd = reinterpret_cast<const Mt76Connac2McuRxd *>(pkt);
    if (rxd->seq != expectedSeq)
        return kErrAgain;

    uint8_t mcuCmd = static_cast<uint8_t>(fieldGet(MCU_CMD_FIELD_ID, cmd));

    if (cmd == MCU_CMD_PATCH_SEM_CONTROL || cmd == MCU_CMD_PATCH_FINISH_REQ) {
        if (len < sizeof(Mt76Connac2McuRxd) - 3)
            return kErrInvalid;
        payloadOffset = sizeof(Mt76Connac2McuRxd) - 4;
        return pkt[payloadOffset];
    }

    if (cmd == MCU_UNI_CMD_DEV_INFO_UPDATE ||
        cmd == MCU_UNI_CMD_BSS_INFO_UPDATE ||
        cmd == MCU_UNI_CMD_STA_REC_UPDATE ||
        cmd == MCU_UNI_CMD_HIF_CTRL ||
        cmd == MCU_UNI_CMD_OFFLOAD ||
        cmd == MCU_UNI_CMD_SUSPEND) {
        if (len < sizeof(Mt76Connac2McuRxd) + sizeof(Mt76ConnacMcuUniEvent))
            return kErrInvalid;
        const auto *ev = reinterpret_cast<const Mt76ConnacMcuUniEvent *>(pkt + sizeof(Mt76Connac2McuRxd));
        if (ev->cid != mcuCmd)
            return kErrAgain;
        payloadOffset = sizeof(Mt76Connac2McuRxd) + sizeof(Mt76ConnacMcuUniEvent);
        int st = static_cast<int>(getLe32(&ev->status));
        if (st != 0)
            MTK_ERR("MCU UNI cmd 0x%02x returned status=0x%x", mcuCmd, st);
        return st;
    }

    payloadOffset = sizeof(Mt76Connac2McuRxd);
    return kOk;
}

void MT7902Mcu::processRxRing0Packet(const uint8_t *pkt, size_t len) {
    if (!pkt || len < 4)
        return;

    uint32_t rxd0 = getLe32(pkt);
    uint8_t pktType = static_cast<uint8_t>(fieldGet(MT_RXD0_PKT_TYPE, rxd0));
    uint8_t pktFlag = static_cast<uint8_t>(fieldGet(MT_RXD0_PKT_FLAG, rxd0));

    if (pktType == PKT_TYPE_RX_EVENT && pktFlag == 0x1)
        pktType = PKT_TYPE_NORMAL_MCU;

    // MT7902 shares RX Ring 0 between MCU events, TX-free, TXS, and MCU/scan RX frames
    // (matching Linux mt7921_queue_rx_skb on MT_RXQ_MCU).
    if (pktType == PKT_TYPE_NORMAL || pktType == PKT_TYPE_NORMAL_MCU) {
        if (mObserver)
            mObserver->onRxNormalPacket(pkt, len);
        return;
    }
    if (pktType == PKT_TYPE_TXRX_NOTIFY) {
        if (mObserver)
            mObserver->onTxFreePacket(pkt, len);
        return;
    }
    if (pktType == PKT_TYPE_TXS) {
        if (mObserver)
            mObserver->onTxStatusPacket(pkt, len);
        return;
    }
    if (pktType != PKT_TYPE_RX_EVENT || len < sizeof(Mt76Connac2McuRxd))
        return;

    const auto *rxd = reinterpret_cast<const Mt76Connac2McuRxd *>(pkt);
    if (rxd->option & MCU_UNI_CMD_UNSOLICITED_EVENT) {
        const uint8_t *body = pkt + sizeof(Mt76Connac2McuRxd);
        size_t bodyLen = len - sizeof(Mt76Connac2McuRxd);
        if (rxd->eid == MCU_UNI_EVENT_ROC) {
            mRocGranted = true;
        } else if ((rxd->eid == MCU_EVENT_SCAN_DONE || rxd->eid == MCU_EVENT_SCHED_SCAN_DONE) &&
                   mObserver && bodyLen >= 1) {
            mObserver->onScanComplete(body[0]);
        }
        return;
    }

    if (rxd->eid == 0x06) {
        if (mWaitingResp && rxd->seq == mWaitSeq) {
            size_t copyLen = len < sizeof(mRespStorage) ? len : sizeof(mRespStorage);
            memcpy(mRespStorage, pkt, copyLen);
            mRespSize = copyLen;
            mRespReceived = true;
        }
        return;
    }

    bool unsolicited = rxd->seq == 0 ||
                       rxd->eid == MCU_EVENT_SCAN_DONE ||
                       rxd->eid == MCU_EVENT_SCHED_SCAN_DONE ||
                       rxd->eid == MCU_EVENT_BSS_BEACON_LOSS ||
                       rxd->eid == MCU_EVENT_RSSI_NOTIFY ||
                       rxd->eid == MCU_EVENT_COREDUMP ||
                       rxd->eid == MCU_EVENT_DBG_MSG ||
                       rxd->eid == MCU_EVENT_LP_INFO ||
                       rxd->eid == MCU_EVENT_TX_DONE;

    if (unsolicited) {
        const uint8_t *body = pkt + sizeof(Mt76Connac2McuRxd);
        size_t bodyLen = len - sizeof(Mt76Connac2McuRxd);
        switch (rxd->eid) {
        case MCU_EVENT_SCAN_DONE:
        case MCU_EVENT_SCHED_SCAN_DONE:
            if (mObserver && bodyLen >= 1)
                mObserver->onScanComplete(body[0]);
            break;
        case MCU_EVENT_BSS_BEACON_LOSS:
            if (mObserver && bodyLen >= 2)
                mObserver->onBeaconLoss(body[0], body[1]);
            break;
        case MCU_EVENT_RSSI_NOTIFY:
            if (mObserver && bodyLen >= 4)
                mObserver->onRssiNotify(static_cast<int32_t>(getLe32(body)));
            break;
        case MCU_EVENT_COREDUMP:
            MTK_ERR("MT7902 firmware assertion / coredump event received");
            if (mObserver)
                mObserver->onFirmwareAssert();
            break;
        default:
            break;
        }
        return;
    }

    if (mWaitingResp && rxd->seq == mWaitSeq) {
        size_t copyLen = len < sizeof(mRespStorage) ? len : sizeof(mRespStorage);
        memcpy(mRespStorage, pkt, copyLen);
        mRespSize = copyLen;
        mRespReceived = true;
    }
}

void MT7902Mcu::drainRxEvents() {
    uint8_t buf[MT7902_RX_BUF_SIZE];
    size_t len = 0;
    for (int i = 0; i < 512; ++i) {
        if (mDma.rxDequeue(kRxMcuEvent, buf, sizeof(buf), len) != kOk)
            break;
        processRxRing0Packet(buf, len);
    }
}

int MT7902Mcu::sendMessage(uint32_t cmd, const void *payload, size_t payloadLen,
                           bool waitResp, uint8_t *respBuf,
                           size_t respCap, size_t *respLen,
                           uint32_t timeoutMs) {
    uint8_t txBuf[4608];
    uint8_t seq = 0;
    size_t msgLen = buildMessage(cmd, payload, payloadLen, txBuf, sizeof(txBuf), seq);
    if (msgLen == 0)
        return kErrInvalid;

    DmaQueueId qid = (cmd == MCU_CMD_FW_SCATTER) ? kTxFwdl : kTxMcuWm;

    if (waitResp) {
        mWaitingResp = true;
        mWaitSeq = seq;
        mWaitCmd = cmd;
        mRespReceived = false;
        mRespSize = 0;
    }

    int err = mDma.txSubmitCopy(qid, txBuf, msgLen);
    if (err != kOk) {
        mWaitingResp = false;
        return err;
    }

    if (!waitResp)
        return kOk;

    uint64_t deadline = mBus.nowMs() + timeoutMs;
    while (!mRespReceived) {
        drainRxEvents();
        if (mRespReceived)
            break;
        if (mBus.nowMs() >= deadline) {
            mWaitingResp = false;
            MTK_ERR("MCU cmd 0x%08x (seq %u) timed out", cmd, seq);
            return kErrTimeout;
        }
        mBus.delayUs(200);
    }

    mWaitingResp = false;
    size_t payloadOff = 0;
    int status = parseResponseHeader(cmd, seq, mRespStorage, mRespSize, payloadOff);

    if (respBuf && respCap > 0 && payloadOff <= mRespSize) {
        size_t avail = mRespSize - payloadOff;
        size_t toCopy = avail < respCap ? avail : respCap;
        if (toCopy > 0)
            memcpy(respBuf, mRespStorage + payloadOff, toCopy);
        if (respLen)
            *respLen = toCopy;
    } else if (respLen) {
        *respLen = (payloadOff <= mRespSize) ? (mRespSize - payloadOff) : 0;
    }

    return status;
}

int MT7902Mcu::restartFirmware() {
    struct __attribute__((packed)) {
        uint8_t power_mode;
        uint8_t rsv[3];
    } req = { 1, { 0, 0, 0 } };
    return sendMessage(MCU_CMD_NIC_POWER_CTRL, &req, sizeof(req), false);
}

int MT7902Mcu::patchSemCtrl(bool acquire, uint8_t &semStatus) {
    uint32_t op = acquire ? PATCH_SEM_GET : PATCH_SEM_RELEASE;
    uint8_t req[4];
    putLe32(req, op);
    int ret = sendMessage(MCU_CMD_PATCH_SEM_CONTROL, req, sizeof(req), true);
    if (ret < 0)
        return ret;
    semStatus = static_cast<uint8_t>(ret);
    return kOk;
}

int MT7902Mcu::initDownload(uint32_t addr, uint32_t len, uint32_t mode) {
    uint8_t req[12];
    putLe32(req + 0, addr);
    putLe32(req + 4, len);
    putLe32(req + 8, mode);

    uint32_t cmd = (addr == MCU_PATCH_ADDRESS || addr == MCU_CONNAC2_PATCH_ADDR)
                       ? MCU_CMD_PATCH_START_REQ
                       : MCU_CMD_TARGET_ADDRESS_LEN_REQ;
    return sendMessage(cmd, req, sizeof(req), true);
}

int MT7902Mcu::sendFirmwareScatter(const uint8_t *data, size_t len, size_t maxChunk) {
    if (!data || len == 0 || maxChunk == 0)
        return kErrInvalid;
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = (len - offset > maxChunk) ? maxChunk : (len - offset);
        int err = sendMessage(MCU_CMD_FW_SCATTER, data + offset, chunk, false);
        if (err != kOk)
            return err;
        offset += chunk;
        // Periodically reclaim completed FWDL TX descriptors
        if ((offset / maxChunk) % 16 == 0)
            mDma.txCleanup(kTxFwdl);
    }
    return kOk;
}

int MT7902Mcu::startPatch(uint8_t &status) {
    uint8_t req[4] = { 0, 0, 0, 0 };
    int ret = sendMessage(MCU_CMD_PATCH_FINISH_REQ, req, sizeof(req), true);
    if (ret < 0)
        return ret;
    status = static_cast<uint8_t>(ret);
    return kOk;
}

int MT7902Mcu::startFirmware(uint32_t overrideAddr, uint32_t option) {
    uint8_t req[8];
    putLe32(req + 0, option);
    putLe32(req + 4, overrideAddr);
    return sendMessage(MCU_CMD_FW_START_REQ, req, sizeof(req), true);
}

int MT7902Mcu::parseNicCapabilityPayload(const uint8_t *payload, size_t len, NicCapability &cap) {
    if (!payload || len < 4)
        return kErrInvalid;

    uint16_t nElem = getLe16(payload);
    size_t pos = 4;

    for (uint16_t i = 0; i < nElem && pos + 8 <= len; ++i) {
        uint32_t tlvType = getLe32(payload + pos);
        uint32_t tlvLen = getLe32(payload + pos + 4);
        pos += 8;
        if (pos + tlvLen > len)
            break;

        const uint8_t *val = payload + pos;
        switch (tlvType) {
        case MT_NIC_CAP_MAC_ADDR:
            if (tlvLen >= 6) {
                memcpy(cap.macAddr, val, 6);
                cap.macValid = true;
            }
            break;
        case MT_NIC_CAP_6G:
            if (tlvLen >= 1)
                cap.has6Ghz = val[0] != 0;
            break;
        case MT_NIC_CAP_PHY:
            if (tlvLen >= 12) {
                // struct mt7921_phy_cap: ht, vht, _5g, max_bw, nss, dbdc, ..., hw_path(10), he(11)
                cap.maxBw = val[3];
                cap.nss = val[4] ? val[4] : 1;
                cap.antennaMask = static_cast<uint8_t>((1U << cap.nss) - 1U);
                cap.has2Ghz = (val[10] & MTK_BIT(0)) != 0;
                cap.has5Ghz = (val[10] & MTK_BIT(1)) != 0;
                cap.supportsHe = val[11] != 0;
            }
            break;
        case MT_NIC_CAP_CHIP_CAP:
            if (tlvLen >= 8) {
                uint64_t lo = getLe32(val);
                uint64_t hi = getLe32(val + 4);
                cap.chipCap = lo | (hi << 32);
            }
            break;
        default:
            break;
        }
        pos += tlvLen;
    }

    return kOk;
}

int MT7902Mcu::getNicCapability(NicCapability &cap) {
    uint8_t resp[1024];
    size_t respLen = 0;
    int err = sendMessage(MCU_CE_CMD_GET_NIC_CAPAB, nullptr, 0, true,
                          resp, sizeof(resp), &respLen);
    if (err != kOk)
        return err;
    return parseNicCapabilityPayload(resp, respLen, cap);
}

int MT7902Mcu::setEepromEfuseMode() {
    struct __attribute__((packed)) {
        uint8_t buffer_mode;
        uint8_t format;
        uint16_t len;
    } req = { 0 /* EE_MODE_EFUSE */, 1 /* EE_FORMAT_WHOLE */, 0 };
    return sendMessage(MCU_EXT_CMD_EFUSE_BUFFER_MODE, &req, sizeof(req), true,
                       nullptr, 0, nullptr, 1500);
}

int MT7902Mcu::setMacEnable(uint8_t band, bool enable) {
    uint8_t req[4] = { static_cast<uint8_t>(enable ? 1 : 0), band, 0, 0 };
    return sendMessage(MCU_EXT_CMD_MAC_INIT_CTRL, req, sizeof(req), true);
}

int MT7902Mcu::setChannelInfo(const ChannelConfig &ch, uint8_t antennaMask, uint32_t cmd) {
    Mt7902ChanSwitchReq req;
    memset(&req, 0, sizeof(req));
    req.control_ch = ch.primaryChannel;
    req.center_ch = ch.centerChannel ? ch.centerChannel : ch.primaryChannel;
    req.bw = ch.bw;
    uint8_t nss = static_cast<uint8_t>(popcount8(antennaMask));
    if (nss == 0) nss = 1;
    req.tx_streams_num = nss;
    req.rx_streams = (cmd == MCU_EXT_CMD_CHANNEL_SWITCH) ? nss : antennaMask;
    req.switch_reason = (cmd == MCU_EXT_CMD_SET_RX_PATH)
                            ? CH_SWITCH_NORMAL
                            : (ch.offchannel ? CH_SWITCH_SCAN_BYPASS_DPD : CH_SWITCH_NORMAL);
    req.band_idx = 0;
    req.center_ch2 = ch.centerChannel2;
    req.channel_band = ch.band;
    return sendMessage(cmd, &req, sizeof(req), true);
}

int MT7902Mcu::setChannel(const ChannelConfig &ch, uint8_t antennaMask) {
    int err = setChannelInfo(ch, antennaMask, MCU_EXT_CMD_CHANNEL_SWITCH);
    if (err != kOk)
        return err;
    return setChannelInfo(ch, antennaMask, MCU_EXT_CMD_SET_RX_PATH);
}

int MT7902Mcu::setRxFilter(uint32_t fif, uint8_t bitOp, uint32_t bitMap) {
    // Exact 68-byte struct matching Linux mt7921_mcu_set_rxfilter()
    struct __attribute__((packed)) {
        uint8_t rsv[4];
        uint8_t mode;
        uint8_t rsv2[3];
        uint32_t fif;
        uint32_t bit_map;
        uint8_t bit_op;
        uint8_t pad[51];
    } req;
    memset(&req, 0, sizeof(req));
    req.mode = fif ? 1 : 2;
    putLe32(&req.fif, fif);
    putLe32(&req.bit_map, bitMap);
    req.bit_op = bitOp;
    return sendMessage(MCU_CE_CMD_SET_RX_FILTER, &req, sizeof(req), false);
}

int MT7902Mcu::setChannelDomain(const char alpha2[2], const ScanChannelEntry *channels, size_t count) {
    struct __attribute__((packed)) DomainHdr {
        char alpha2[4];
        uint8_t bw_2g;
        uint8_t bw_5g;
        uint8_t bw_6g;
        uint8_t pad;
        uint8_t n_2ch;
        uint8_t n_5ch;
        uint8_t n_6ch;
        uint8_t pad2;
    };
    struct __attribute__((packed)) DomainChan {
        uint16_t hw_value;
        uint16_t pad;
        uint32_t flags;
    };

    uint8_t buf[sizeof(DomainHdr) + 96 * sizeof(DomainChan)];
    memset(buf, 0, sizeof(buf));
    auto *hdr = reinterpret_cast<DomainHdr *>(buf);
    hdr->alpha2[0] = alpha2[0];
    hdr->alpha2[1] = alpha2[1];
    hdr->bw_2g = 0; // 20/40 MHz
    hdr->bw_5g = 3; // 20/40/80/160 MHz
    hdr->bw_6g = 3;

    size_t outIdx = 0;
    auto *chOut = reinterpret_cast<DomainChan *>(buf + sizeof(DomainHdr));
    for (uint8_t band = 1; band <= 3; ++band) {
        for (size_t i = 0; i < count && outIdx < 96; ++i) {
            if (channels[i].band != band)
                continue;
            putLe16(&chOut[outIdx].hw_value, channels[i].channel_num);
            putLe32(&chOut[outIdx].flags, 0);
            ++outIdx;
            if (band == 1) hdr->n_2ch++;
            else if (band == 2) hdr->n_5ch++;
            else hdr->n_6ch++;
        }
    }

    return sendMessage(MCU_CE_CMD_SET_CHAN_DOMAIN, buf,
                       sizeof(DomainHdr) + outIdx * sizeof(DomainChan), false);
}

int MT7902Mcu::setDeepSleep(bool enable) {
    struct __attribute__((packed)) {
        uint8_t cmd;
        uint8_t action;
        uint8_t mode;
        uint8_t rsv;
    } req = { 0x0b, static_cast<uint8_t>(enable ? 1 : 0), 0, 0 };
    return sendMessage(MCU_CE_CMD_SET_PS_PROFILE, &req, sizeof(req), false);
}

int MT7902Mcu::setHifSuspend(bool suspend) {
    struct __attribute__((packed)) {
        uint8_t hif_type; // 2 = HIF_PCIE
        uint8_t pad[3];
        uint16_t tag;
        uint16_t len;
        uint8_t suspend;
        uint8_t pad2[7];
    } req;
    memset(&req, 0, sizeof(req));
    req.hif_type = 2;
    putLe16(&req.tag, 0);
    putLe16(&req.len, 12);
    req.suspend = suspend ? 1 : 0;
    return sendMessage(MCU_UNI_CMD_HIF_CTRL, &req, sizeof(req), true);
}

int MT7902Mcu::setRtsThreshold(uint32_t val, uint8_t band) {
    struct __attribute__((packed)) {
        uint8_t prot_idx;
        uint8_t band;
        uint8_t rsv[2];
        uint32_t len_thresh;
        uint32_t pkt_thresh;
    } req;
    memset(&req, 0, sizeof(req));
    req.prot_idx = 1;
    req.band = band;
    putLe32(&req.len_thresh, val);
    putLe32(&req.pkt_thresh, 2);
    return sendMessage(MCU_EXT_CMD_PROTECT_CTRL, &req, sizeof(req), true);
}

int MT7902Mcu::setEdcaParams(uint8_t bssIdx, bool qos) {
    // Match Linux mt7921_mcu_set_tx() WMM/EDCA defaults (ACI order: BE=0, BK=1, VI=2, VO=3)
    Mt7902EdcaReq req;
    memset(&req, 0, sizeof(req));
    static constexpr uint16_t kAifs[4]  = { 3, 7, 2, 2 };
    static constexpr uint16_t kCwMin[4] = { 4, 4, 3, 2 };
    static constexpr uint16_t kCwMax[4] = { 10, 10, 4, 3 };
    static constexpr uint16_t kTxop[4]  = { 0, 0, 94, 47 };
    for (int ac = 0; ac < 4; ++ac) {
        putLe16(&req.edca[ac].cw_min, kCwMin[ac]);
        putLe16(&req.edca[ac].cw_max, kCwMax[ac]);
        putLe16(&req.edca[ac].txop, kTxop[ac]);
        putLe16(&req.edca[ac].aifs, kAifs[ac]);
    }
    req.bss_idx = bssIdx;
    req.qos = qos ? 1 : 0;
    req.wmm_idx = 0;
    return sendMessage(MCU_CE_CMD_SET_EDCA_PARMS, &req, sizeof(req), false);
}

int MT7902Mcu::uniAddDev(uint8_t omacIdx, uint8_t bssIdx, uint16_t bmcWcid,
                         const uint8_t macAddr[6], bool enable) {
    UniDevInfoReq devReq;
    memset(&devReq, 0, sizeof(devReq));
    devReq.omac_idx = omacIdx;
    devReq.band_idx = 0;
    putLe16(&devReq.tag, DEV_INFO_ACTIVE);
    putLe16(&devReq.len, 12);
    devReq.active = enable ? 1 : 0;
    memcpy(devReq.omac_addr, macAddr, 6);

    struct __attribute__((packed)) {
        uint8_t bss_idx;
        uint8_t pad[3];
        BssBasicTlv basic;
    } bssReq;
    memset(&bssReq, 0, sizeof(bssReq));
    bssReq.bss_idx = bssIdx;
    putLe16(&bssReq.basic.tag, UNI_BSS_INFO_BASIC);
    putLe16(&bssReq.basic.len, sizeof(BssBasicTlv));
    bssReq.basic.active = enable ? 1 : 0;
    bssReq.basic.omac_idx = omacIdx;
    bssReq.basic.hw_bss_idx = omacIdx;
    bssReq.basic.band_idx = 0;
    putLe32(&bssReq.basic.conn_type, CONNECTION_INFRA_STA);
    bssReq.basic.conn_state = 1;
    putLe16(&bssReq.basic.bmc_tx_wlan_idx, bmcWcid);
    putLe16(&bssReq.basic.sta_idx, bmcWcid);

    if (enable) {
        int err = sendMessage(MCU_UNI_CMD_DEV_INFO_UPDATE, &devReq, sizeof(devReq), true);
        if (err != kOk) return err;
        err = sendMessage(MCU_UNI_CMD_BSS_INFO_UPDATE, &bssReq, sizeof(bssReq), true);
        return (err < 0) ? err : kOk;
    } else {
        int err = sendMessage(MCU_UNI_CMD_BSS_INFO_UPDATE, &bssReq, sizeof(bssReq), true);
        if (err < 0) return err;
        err = sendMessage(MCU_UNI_CMD_DEV_INFO_UPDATE, &devReq, sizeof(devReq), true);
        return (err < 0) ? err : kOk;
    }
}

int MT7902Mcu::uniSetChctx(uint8_t bssIdx, const ChannelConfig &ch, uint8_t antennaMask) {
    struct __attribute__((packed)) {
        uint8_t bss_idx;
        uint8_t pad[3];
        BssRlmTlv rlm;
    } req;
    memset(&req, 0, sizeof(req));
    req.bss_idx = bssIdx;
    putLe16(&req.rlm.tag, UNI_BSS_INFO_RLM);
    putLe16(&req.rlm.len, sizeof(BssRlmTlv));
    req.rlm.control_channel = ch.primaryChannel;
    uint8_t center = ch.centerChannel ? ch.centerChannel : ch.primaryChannel;
    req.rlm.center_chan = center;
    req.rlm.center_chan2 = ch.centerChannel2;
    req.rlm.bw = ch.bw;
    uint8_t nss = static_cast<uint8_t>(popcount8(antennaMask));
    if (nss == 0) nss = 1;
    req.rlm.tx_streams = nss;
    req.rlm.rx_streams = antennaMask ? antennaMask : 1;
    req.rlm.short_st = 1;
    req.rlm.ht_op_info = (ch.bw == CMD_CBW_20MHZ) ? 0 : 4;
    if (ch.primaryChannel < center)
        req.rlm.sco = 1; // SCA
    else if (ch.primaryChannel > center)
        req.rlm.sco = 3; // SCB
    req.rlm.band = ch.band; // NL80211_BAND_* (0=2G, 1=5G, 2=6G)
    return sendMessage(MCU_UNI_CMD_BSS_INFO_UPDATE, &req, sizeof(req), true);
}

int MT7902Mcu::uniAddBss(uint8_t omacIdx, uint8_t bssIdx, uint16_t bmcWcid,
                         const uint8_t bssid[6], const ChannelConfig &ch,
                         uint16_t bcnInterval, uint8_t dtimPeriod, bool qos, bool enable) {
    struct __attribute__((packed)) {
        uint8_t bss_idx;
        uint8_t pad[3];
        BssBasicTlv basic;
        BssQosTlv qos;
    } req;
    memset(&req, 0, sizeof(req));
    req.bss_idx = bssIdx;

    putLe16(&req.basic.tag, UNI_BSS_INFO_BASIC);
    putLe16(&req.basic.len, sizeof(BssBasicTlv));
    req.basic.active = 1;
    req.basic.omac_idx = omacIdx;
    req.basic.hw_bss_idx = omacIdx;
    req.basic.band_idx = 0;
    putLe32(&req.basic.conn_type, CONNECTION_INFRA_STA);
    req.basic.conn_state = enable ? 0 : 1;
    req.basic.wmm_idx = 0;
    memcpy(req.basic.bssid, bssid, 6);
    putLe16(&req.basic.bmc_tx_wlan_idx, bmcWcid);
    putLe16(&req.basic.bcn_interval, bcnInterval ? bcnInterval : 100);
    req.basic.dtim_period = dtimPeriod ? dtimPeriod : 1;
    if (ch.band == 0) {
        req.basic.phymode = PHY_MODE_B | PHY_MODE_G | PHY_MODE_GN | PHY_MODE_AX_24G;
        putLe16(&req.basic.nonht_basic_phy,
                PHY_TYPE_BIT_HR_DSSS | PHY_TYPE_BIT_ERP | PHY_TYPE_BIT_HT | PHY_TYPE_BIT_HE);
    } else if (ch.band == 2) {
        req.basic.phymode = PHY_MODE_A | PHY_MODE_AN | PHY_MODE_AC;
        req.basic.phymode_ext = PHY_MODE_AX_6G;
        putLe16(&req.basic.nonht_basic_phy,
                PHY_TYPE_BIT_OFDM | PHY_TYPE_BIT_HT | PHY_TYPE_BIT_VHT | PHY_TYPE_BIT_HE);
    } else {
        req.basic.phymode = PHY_MODE_A | PHY_MODE_AN | PHY_MODE_AC | PHY_MODE_AX_5G;
        putLe16(&req.basic.nonht_basic_phy,
                PHY_TYPE_BIT_OFDM | PHY_TYPE_BIT_HT | PHY_TYPE_BIT_VHT | PHY_TYPE_BIT_HE);
    }
    putLe16(&req.basic.sta_idx, bmcWcid);

    putLe16(&req.qos.tag, UNI_BSS_INFO_QBSS);
    putLe16(&req.qos.len, sizeof(BssQosTlv));
    req.qos.qos = qos ? 1 : 0;

    int err = sendMessage(MCU_UNI_CMD_BSS_INFO_UPDATE, &req, sizeof(req), true);
    if (err != kOk)
        return err;
    if (enable)
        return uniSetChctx(bssIdx, ch, 1);
    return kOk;
}

int MT7902Mcu::uniUpdateSta(const StaUpdateParams &p) {
    // Wire-exact match to Linux mt76_connac_mcu_sta_cmd() with offload_fw = true
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    size_t pos = 0;

    auto *hdr = reinterpret_cast<StaReqHdr *>(buf);
    pos += sizeof(StaReqHdr);
    hdr->bss_idx = p.bssIdx;
    hdr->wlan_idx_lo = static_cast<uint8_t>(p.wcid & 0xffU);
    hdr->wlan_idx_hi = static_cast<uint8_t>((p.wcid >> 8) & 0xffU);
    hdr->is_tlv_append = 1;
    hdr->muar_idx = p.broadcast ? 0x0eU : 0x00U;
    uint16_t topTlvs = 0;

    // 1. STA_REC_BASIC (non-BMC peer STA only; BMC uses offload_fw = true -> WTBL only)
    if (!p.broadcast) {
        auto *basic = reinterpret_cast<StaRecBasicTlv *>(buf + pos);
        pos += sizeof(StaRecBasicTlv);
        putLe16(&basic->tag, STA_REC_BASIC);
        putLe16(&basic->len, sizeof(StaRecBasicTlv));
        putLe32(&basic->conn_type, CONNECTION_INFRA_AP);
        basic->conn_state = p.enable ? CONN_STATE_PORT_SECURE : CONN_STATE_DISCONNECT;
        basic->qos = p.qos ? 1 : 0;
        putLe16(&basic->aid, p.aid);
        memcpy(basic->peer_addr, p.peerMac, 6);
        uint16_t extra = MTK_BIT(0); // EXTRA_INFO_VER
        if (p.newly && p.enable)
            extra |= MTK_BIT(1);     // EXTRA_INFO_NEW
        putLe16(&basic->extra_info, extra);
        topTlvs++;
    }

    // 2. Peer STA capability / PHY / RA / STATE TLVs (only when enabling non-BMC peer)
    if (!p.broadcast && p.enable) {
        if (p.ht) {
            auto *ht = reinterpret_cast<StaRecHtTlv *>(buf + pos);
            pos += sizeof(StaRecHtTlv);
            putLe16(&ht->tag, STA_REC_HT);
            putLe16(&ht->len, sizeof(StaRecHtTlv));
            putLe16(&ht->ht_cap, 0x01ef);
            topTlvs++;
        }

        auto *phy = reinterpret_cast<StaRecPhyTlv *>(buf + pos);
        pos += sizeof(StaRecPhyTlv);
        putLe16(&phy->tag, STA_REC_PHY);
        putLe16(&phy->len, sizeof(StaRecPhyTlv));
        if (p.band == 0) {
            putLe16(&phy->basic_rate, 0x000f);
            phy->phy_type = static_cast<uint8_t>(
                PHY_TYPE_BIT_HR_DSSS | PHY_TYPE_BIT_ERP | (p.ht ? PHY_TYPE_BIT_HT : 0));
        } else {
            putLe16(&phy->basic_rate, 0x0015);
            phy->phy_type = static_cast<uint8_t>(
                PHY_TYPE_BIT_OFDM | (p.ht ? PHY_TYPE_BIT_HT : 0));
        }
        phy->ampdu = 0x1b;
        phy->rcpi = rssiToRcpi(p.rssi);
        topTlvs++;

        auto *ra = reinterpret_cast<StaRecRaInfoTlv *>(buf + pos);
        pos += sizeof(StaRecRaInfoTlv);
        putLe16(&ra->tag, STA_REC_RA);
        putLe16(&ra->len, sizeof(StaRecRaInfoTlv));
        uint16_t legacy = (p.band == 0)
                              ? static_cast<uint16_t>((0xffU << 6) | 0x0fU)
                              : static_cast<uint16_t>(0xffU << 6);
        putLe16(&ra->legacy, legacy);
        if (p.ht)
            ra->rx_mcs_bitmask[0] = 0xff;
        topTlvs++;

        auto *st = reinterpret_cast<StaRecStateTlv *>(buf + pos);
        pos += sizeof(StaRecStateTlv);
        putLe16(&st->tag, STA_REC_STATE);
        putLe16(&st->len, sizeof(StaRecStateTlv));
        st->state = p.staState;
        topTlvs++;
    }

    // 3. STA_REC_WTBL container (mandatory on Connac2 for both add/update and remove)
    auto *staWtbl = reinterpret_cast<McuTlvHdr *>(buf + pos);
    pos += sizeof(McuTlvHdr);
    putLe16(&staWtbl->tag, STA_REC_WTBL);
    uint16_t wtblContainerLen = sizeof(McuTlvHdr);
    topTlvs++;

    auto *wtblHdr = reinterpret_cast<WtblReqHdr *>(buf + pos);
    pos += sizeof(WtblReqHdr);
    wtblContainerLen += sizeof(WtblReqHdr);
    wtblHdr->wlan_idx_lo = static_cast<uint8_t>(p.wcid & 0xffU);
    wtblHdr->wlan_idx_hi = static_cast<uint8_t>((p.wcid >> 8) & 0xffU);
    wtblHdr->operation = WTBL_RESET_AND_SET;
    uint16_t wtblTlvs = 0;

    if (p.enable) {
        auto *gen = reinterpret_cast<WtblGenericTlv *>(buf + pos);
        pos += sizeof(WtblGenericTlv);
        wtblContainerLen += sizeof(WtblGenericTlv);
        putLe16(&gen->tag, WTBL_GENERIC);
        putLe16(&gen->len, sizeof(WtblGenericTlv));
        memcpy(gen->peer_addr, p.peerMac, 6);
        gen->muar_idx = p.broadcast ? 0x0eU : 0x00U;
        gen->qos = (!p.broadcast && p.qos) ? 1 : 0;
        putLe16(&gen->partial_aid, p.broadcast ? 0 : p.aid);
        wtblTlvs++;

        auto *rx = reinterpret_cast<WtblRxTlv *>(buf + pos);
        pos += sizeof(WtblRxTlv);
        wtblContainerLen += sizeof(WtblRxTlv);
        putLe16(&rx->tag, WTBL_RX);
        putLe16(&rx->len, sizeof(WtblRxTlv));
        rx->rca1 = 1;
        rx->rca2 = 1;
        rx->rv = 1;
        wtblTlvs++;

        auto *htr = reinterpret_cast<WtblHdrTransTlv *>(buf + pos);
        pos += sizeof(WtblHdrTransTlv);
        wtblContainerLen += sizeof(WtblHdrTransTlv);
        putLe16(&htr->tag, WTBL_HDR_TRANS);
        putLe16(&htr->len, sizeof(WtblHdrTransTlv));
        htr->to_ds = 1;
        htr->from_ds = 0;
        htr->no_rx_trans = 0; // Enable Connac2 HW 802.3 header translation for STA RX
        wtblTlvs++;

        if (!p.broadcast && p.ht) {
            auto *wht = reinterpret_cast<WtblHtTlv *>(buf + pos);
            pos += sizeof(WtblHtTlv);
            wtblContainerLen += sizeof(WtblHtTlv);
            putLe16(&wht->tag, WTBL_HT);
            putLe16(&wht->len, sizeof(WtblHtTlv));
            wht->ht = 1;
            wht->ldpc = 1;
            wht->af = 3;
            wht->mm = 3;
            wtblTlvs++;
        }
    }

    putLe16(&wtblHdr->tlv_num, wtblTlvs);
    putLe16(&staWtbl->len, wtblContainerLen);
    putLe16(&hdr->tlv_num, topTlvs);

    return sendMessage(MCU_UNI_CMD_STA_REC_UPDATE, buf, pos, true);
}

int MT7902Mcu::acquireRoc(uint8_t bssIdx, const ChannelConfig &ch,
                          uint32_t durationMs, bool joinReq) {
    if (mRocActive)
        (void)abortRoc(bssIdx);

    mRocTokenId = static_cast<uint8_t>(mRocTokenId + 1U);
    if (mRocTokenId == 0)
        mRocTokenId = 1;
    mRocGranted = false;

    RocAcquireReq req;
    memset(&req, 0, sizeof(req));
    putLe16(&req.tag, UNI_ROC_ACQUIRE);
    putLe16(&req.len, static_cast<uint16_t>(sizeof(RocAcquireReq) - sizeof(req.rsv_hdr)));
    req.bss_idx = bssIdx;
    req.tokenid = mRocTokenId;
    req.control_channel = ch.primaryChannel;
    uint8_t center = ch.centerChannel ? ch.centerChannel : ch.primaryChannel;
    if (ch.primaryChannel < center)
        req.sco = 1;
    else if (ch.primaryChannel > center)
        req.sco = 3;
    req.band = static_cast<uint8_t>(ch.band + 1U); // 1=2G, 2=5G, 3=6G (mt7921_mcu_set_roc)
    req.bw = CMD_CBW_20MHZ;
    req.center_chan = center;
    req.bw_from_ap = CMD_CBW_20MHZ;
    req.center_chan_from_ap = center;
    req.reqtype = joinReq ? MT7921_ROC_REQ_JOIN : MT7921_ROC_REQ_ROC;
    putLe32(&req.maxinterval, durationMs ? durationMs : 1000U);
    req.dbdcband = 0xff; // auto

    int err = sendMessage(MCU_UNI_CMD_ROC, &req, sizeof(req), false);
    if (err != kOk)
        return err;

    mRocActive = true;
    uint64_t deadline = mBus.nowMs() + 500ULL;
    while (!mRocGranted && mBus.nowMs() < deadline) {
        drainRxEvents();
        if (mRocGranted)
            break;
        mBus.delayUs(500);
    }
    if (!mRocGranted) {
        MTK_LOG("ROC grant event not seen within 500ms (ch=%u tok=%u), continuing",
                ch.primaryChannel, mRocTokenId);
    }
    return kOk;
}

int MT7902Mcu::abortRoc(uint8_t bssIdx) {
    if (!mRocActive)
        return kOk;
    mRocActive = false;
    mRocGranted = false;

    RocAbortReq req;
    memset(&req, 0, sizeof(req));
    putLe16(&req.tag, UNI_ROC_ABORT);
    putLe16(&req.len, static_cast<uint16_t>(sizeof(RocAbortReq) - sizeof(req.rsv_hdr)));
    req.bss_idx = bssIdx;
    req.tokenid = mRocTokenId;
    req.dbdcband = 0xff;
    return sendMessage(MCU_UNI_CMD_ROC, &req, sizeof(req), false);
}

int MT7902Mcu::uniSetKey(const KeyInstallParams &p) {
    struct __attribute__((packed)) {
        StaReqHdr hdr;
        StaRecSecTlv sec;
    } req;
    memset(&req, 0, sizeof(req));

    req.hdr.bss_idx = p.bssIdx;
    req.hdr.wlan_idx_lo = static_cast<uint8_t>(p.wcid & 0xffU);
    req.hdr.wlan_idx_hi = static_cast<uint8_t>((p.wcid >> 8) & 0xffU);
    putLe16(&req.hdr.tlv_num, 1);
    req.hdr.is_tlv_append = 1;

    putLe16(&req.sec.tag, STA_REC_KEY_V2);
    req.sec.add = p.setKey ? 0 : 1; // Linux SET_KEY = 0, DISABLE_KEY = 1

    size_t secLen = sizeof(StaRecSecTlv);
    if (p.setKey) {
        if (p.keyLen > sizeof(req.sec.key[0].key))
            return kErrInvalid;
        req.sec.n_cipher = 1;
        req.sec.key[0].cipher_id = p.cipher;
        req.sec.key[0].cipher_len = sizeof(SecKeyEntry);
        req.sec.key[0].key_id = p.keyIdx;
        req.sec.key[0].key_len = p.keyLen;
        memcpy(req.sec.key[0].key, p.key, p.keyLen);
        if (p.cipher == MCU_CIPHER_TKIP && p.keyLen == 32) {
            // MT76 swaps TKIP TX/RX MIC keys in HW table
            memcpy(req.sec.key[0].key + 16, p.key + 24, 8);
            memcpy(req.sec.key[0].key + 24, p.key + 16, 8);
        }
        secLen -= sizeof(SecKeyEntry);
    } else {
        req.sec.n_cipher = 0;
        secLen -= sizeof(req.sec.key);
    }
    putLe16(&req.sec.len, static_cast<uint16_t>(secLen));

    return sendMessage(MCU_UNI_CMD_STA_REC_UPDATE, &req,
                       sizeof(StaReqHdr) + secLen, true);
}

int MT7902Mcu::setBssConnected(uint8_t bssIdx, uint16_t aid, uint16_t bcnInterval,
                               uint8_t dtimPeriod, bool enable) {
    uint8_t abortReq[4] = { bssIdx, 0, 0, 0 };
    int err = sendMessage(MCU_CE_CMD_SET_BSS_ABORT, abortReq, sizeof(abortReq), false);
    if (err != kOk || !enable)
        return err;

    struct __attribute__((packed)) {
        uint8_t bss_idx;
        uint8_t dtim_period;
        uint16_t aid;
        uint16_t bcn_interval;
        uint16_t atim_window;
        uint8_t uapsd;
        uint8_t bmc_delivered_ac;
        uint8_t bmc_triggered_ac;
        uint8_t pad;
    } req;
    memset(&req, 0, sizeof(req));
    req.bss_idx = bssIdx;
    req.dtim_period = dtimPeriod ? dtimPeriod : 1;
    putLe16(&req.aid, aid);
    putLe16(&req.bcn_interval, bcnInterval ? bcnInterval : 100);
    return sendMessage(MCU_CE_CMD_SET_BSS_CONNECTED, &req, sizeof(req), false);
}

int MT7902Mcu::startHwScan(const ScanRequestParams &params) {
    Mt7902HwScanReq req;
    memset(&req, 0, sizeof(req));

    mScanSeq = static_cast<uint8_t>((mScanSeq + 1U) & 0x7fU);
    if (mScanSeq == 0) mScanSeq = 1;

    req.seq_num = static_cast<uint8_t>(mScanSeq | (params.bandIdx << 7));
    req.bss_idx = params.bssIdx;
    req.scan_type = params.active ? 1 : 0;
    req.probe_req_num = params.active ? 2 : 0;
    req.scan_func = SCAN_FUNC_SPLIT_SCAN;
    req.version = 1;

    uint8_t validSsids = 0;
    for (uint8_t i = 0; i < params.ssidCount && i < 4; ++i) {
        if (params.ssidLens[i] == 0 || params.ssidLens[i] > 32)
            continue;
        putLe32(&req.ssids[validSsids].ssid_len, params.ssidLens[i]);
        memcpy(req.ssids[validSsids].ssid, params.ssids[i], params.ssidLens[i]);
        ++validSsids;
    }
    req.ssid_type = validSsids ? MTK_BIT(2) : MTK_BIT(0);
    req.ssid_type_ext = validSsids ? MTK_BIT(0) : 0;
    req.ssids_num = validSsids;

    // Connac2 firmware (MT7902/MT7921) uses duration=0 for optimal split-scan timing
    // (matching Linux mt76_connac_mcu_hw_scan: duration = is_connac2 ? 0 : 60)
    putLe16(&req.channel_dwell_time, 0);
    putLe16(&req.channel_min_dwell_time, 0);
    putLe16(&req.timeout_value, 0);

    if (params.channelCount == 0 || params.channelCount > 64) {
        req.channel_type = 0; // Full channel scan
    } else {
        req.channel_type = 4; // Explicit channel list
        req.channels_num = params.channelCount > 32 ? 32 : params.channelCount;
        req.ext_channels_num = params.channelCount > 32
                                   ? static_cast<uint8_t>(params.channelCount - 32)
                                   : 0;
        for (uint8_t i = 0; i < req.channels_num; ++i)
            req.channels[i] = params.channels[i];
        for (uint8_t i = 0; i < req.ext_channels_num; ++i)
            req.ext_channels[i] = params.channels[32 + i];
    }

    memcpy(req.bssid, params.bssid, 6);
    return sendMessage(MCU_CE_CMD_START_HW_SCAN, &req, sizeof(req), false);
}

int MT7902Mcu::cancelHwScan(uint8_t seqNum) {
    uint8_t req[4] = { seqNum, 0, 0, 0 };
    return sendMessage(MCU_CE_CMD_CANCEL_HW_SCAN, req, sizeof(req), false);
}

} // namespace mt7902
