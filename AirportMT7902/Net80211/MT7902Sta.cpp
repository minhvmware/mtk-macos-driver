// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902Sta.hpp"

namespace mt7902 {

MT7902Sta::MT7902Sta(MT7902Device &dev) : mDev(dev) {
    mDev.setRxCallback(this);
}

uint16_t MT7902Sta::nextMgmtSeq() {
    mMgmtSeq = static_cast<uint16_t>((mMgmtSeq + 1U) & 0x0fffU);
    return static_cast<uint16_t>(mMgmtSeq << 4);
}

int MT7902Sta::setPower(bool on) {
    if (on == mPowerOn)
        return kOk;

    if (on) {
        int err = mDev.startRadio();
        if (err != kOk)
            return err;
        mPowerOn = true;
        // Apply current channel domain
        (void)setCountryCode(mCountryCode);
        return kOk;
    } else {
        if (mLink.state >= kStaAuthenticating || mStaAdded || mBssActive)
            (void)disassociate(8);
        mScanning = false;
        int err = mDev.stopRadio();
        mPowerOn = false;
        mLink.state = kStaInit;
        return err;
    }
}

int MT7902Sta::setMacAddress(const uint8_t mac[6]) {
    if (!mac || (mac[0] & 0x01U))
        return kErrInvalid;
    if (memcmp(mDev.capability().macAddr, mac, 6) == 0)
        return kOk;

    if (mScanning)
        (void)abortScan(false);
    if (mLink.state >= kStaAuthenticating || mStaAdded || mBssActive)
        (void)disassociate(8);

    int err = mDev.setMacAddress(mac);
    if (err == kOk && mPowerOn)
        (void)setCountryCode(mCountryCode);
    return err;
}

int MT7902Sta::setCountryCode(const char cc[2]) {
    if (!cc || cc[0] == '\0' || cc[1] == '\0')
        return kErrInvalid;
    mCountryCode[0] = cc[0];
    mCountryCode[1] = cc[1];
    mCountryCode[2] = '\0';

    if (!mPowerOn)
        return kOk;

    ScanChannelEntry chList[64];
    const auto &cap = mDev.capability();
    size_t n = buildScanChannelList(cap.has2Ghz, cap.has5Ghz, cap.has6Ghz, chList, 64);
    return mDev.mcu().setChannelDomain(mCountryCode, chList, n);
}

int MT7902Sta::startScan(const ScanRequestParams &params) {
    if (!mPowerOn)
        return kErrIO;
    if (mScanning)
        return kErrBusy;

    ScanRequestParams req = params;
    if (req.channelCount == 0) {
        const auto &cap = mDev.capability();
        req.channelCount = static_cast<uint8_t>(
            buildScanChannelList(cap.has2Ghz, cap.has5Ghz, cap.has6Ghz, req.channels, 64));
    }

    // Ensure RMAC accepts beacons from other BSSs during scan (especially 5 GHz DFS channels
    // while already associated to another AP where SET_BSS_CONNECTED sets DROP_OTHER_BEACON).
    if (mBssActive) {
        (void)mDev.mcu().setRxFilter(0, MTK_BIT(1), MT_WF_RFCR_DROP_OTHER_BEACON);
        mDev.bus().clear(MT_WF_RFCR(0),
                         MT_WF_RFCR_DROP_OTHER_BEACON | MT_WF_RFCR_DROP_OTHER_BSS | MT_WF_RFCR_DROP_OTHER_UC);
    }

    int err = mDev.mcu().startHwScan(req);
    if (err == kOk) {
        mScanning = true;
        if (mLink.state == kStaInit)
            mLink.state = kStaScanning;
    }
    return err;
}

void MT7902Sta::restoreOperatingChannel() {
    if (!mPowerOn || !mBssActive || mLink.state < kStaAssociated)
        return;
    ChannelConfig ch;
    ch.primaryChannel = mLink.channel;
    ch.centerChannel = mLink.centerChannel ? mLink.centerChannel : mLink.channel;
    ch.band = static_cast<uint8_t>(mLink.band);
    ch.bw = mLink.bw;
    (void)mDev.mcu().setChannel(ch, mDev.capability().antennaMask);
    mDev.mac().setTiming(ch.band == 0, 9);
    (void)mDev.mcu().uniSetChctx(0, ch, mDev.capability().antennaMask);
}

int MT7902Sta::abortScan(bool restoreChan) {
    if (!mScanning)
        return kOk;
    mScanning = false;
    if (mLink.state == kStaScanning)
        mLink.state = kStaInit;
    int err = mDev.mcu().cancelHwScan(mDev.mcu().lastScanSeq());
    if (restoreChan)
        restoreOperatingChannel();
    return err;
}

size_t MT7902Sta::buildAuthReqFrame(const uint8_t staMac[6], const uint8_t bssid[6],
                                    uint16_t seqCtrl, uint16_t authAlg, uint16_t authSeq,
                                    uint8_t *out, size_t outCap) {
    if (!out || outCap < 30)
        return 0;
    memset(out, 0, 30);
    // FC: Management (0), Auth (0x0b -> 0x00b0)
    putLe16(out + 0, 0x00b0);
    putLe16(out + 2, 0x013a); // Duration
    memcpy(out + 4, bssid, 6);  // Addr1 = DA (AP)
    memcpy(out + 10, staMac, 6); // Addr2 = SA (STA)
    memcpy(out + 16, bssid, 6); // Addr3 = BSSID
    putLe16(out + 22, seqCtrl);
    // Fixed auth body (6 bytes)
    putLe16(out + 24, authAlg); // 0 = Open System
    putLe16(out + 26, authSeq); // 1 = Auth seq 1
    putLe16(out + 28, 0);       // Status = 0
    return 30;
}

size_t MT7902Sta::buildAssocReqFrame(const uint8_t staMac[6], const uint8_t bssid[6],
                                     uint16_t seqCtrl, const uint8_t *ssid, uint8_t ssidLen,
                                     const uint8_t *rsnIe, uint16_t rsnIeLen,
                                     bool ht, bool vht, bool he,
                                     uint8_t *out, size_t outCap,
                                     bool fiveGhz, uint8_t bw) {
    if (!out || outCap < 128 || ssidLen > 32)
        return 0;
    memset(out, 0, 28);
    // FC: Management (0), Assoc Request (0x00 -> 0x0000)
    putLe16(out + 0, 0x0000);
    putLe16(out + 2, 0x013a);
    memcpy(out + 4, bssid, 6);
    memcpy(out + 10, staMac, 6);
    memcpy(out + 16, bssid, 6);
    putLe16(out + 22, seqCtrl);

    // Capability info (ESS + Short Preamble + Short Slot) + Listen Interval (10)
    uint16_t capab = fiveGhz ? 0x0401 : 0x0421;
    if (rsnIeLen > 0)
        capab |= 0x0010; // Privacy
    putLe16(out + 24, capab);
    putLe16(out + 26, 10);

    size_t pos = 28;
    // SSID IE (ID=0)
    if (pos + 2 + ssidLen > outCap) return 0;
    out[pos++] = 0;
    out[pos++] = ssidLen;
    if (ssidLen > 0 && ssid) {
        memcpy(out + pos, ssid, ssidLen);
        pos += ssidLen;
    }

    if (fiveGhz) {
        // 5 GHz Supported Rates IE (ID=1): 6*, 9, 12*, 18, 24*, 36, 48, 54 Mbps (OFDM only)
        static constexpr uint8_t kSuppRates5G[] = { 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c };
        if (pos + 2 + sizeof(kSuppRates5G) > outCap) return 0;
        out[pos++] = 1;
        out[pos++] = sizeof(kSuppRates5G);
        memcpy(out + pos, kSuppRates5G, sizeof(kSuppRates5G));
        pos += sizeof(kSuppRates5G);
    } else {
        // 2.4 GHz Supported Rates IE (ID=1): 1, 2, 5.5, 11, 6, 9, 12, 18 Mbps
        static constexpr uint8_t kSuppRates[] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
        if (pos + 2 + sizeof(kSuppRates) > outCap) return 0;
        out[pos++] = 1;
        out[pos++] = sizeof(kSuppRates);
        memcpy(out + pos, kSuppRates, sizeof(kSuppRates));
        pos += sizeof(kSuppRates);

        // Extended Supported Rates IE (ID=50): 24, 36, 48, 54 Mbps
        static constexpr uint8_t kExtRates[] = { 0x30, 0x48, 0x60, 0x6c };
        if (pos + 2 + sizeof(kExtRates) > outCap) return 0;
        out[pos++] = 50;
        out[pos++] = sizeof(kExtRates);
        memcpy(out + pos, kExtRates, sizeof(kExtRates));
        pos += sizeof(kExtRates);
    }

    // RSN / WPA IE (provided by Apple80211 supplicant)
    if (rsnIe && rsnIeLen > 0 && pos + rsnIeLen <= outCap) {
        memcpy(out + pos, rsnIe, rsnIeLen);
        pos += rsnIeLen;
    }

    // HT Capabilities IE (ID=45, 26 bytes)
    if (ht && pos + 28 <= outCap) {
        out[pos++] = 45;
        out[pos++] = 26;
        uint8_t htCap[26] = {};
        // 1SS: LDPC, SGI20, RX_STBC_1SS (no TX_STBC for 1SS); 40MHz + SGI40 only when bw >= 40MHz
        uint16_t htCapInfo = (bw >= CMD_CBW_40MHZ) ? 0x016fU : 0x012dU;
        putLe16(htCap + 0, htCapInfo);
        htCap[2] = 0x1b;            // A-MPDU params
        htCap[3] = 0xff;            // MCS 0..7 (1SS)
        memcpy(out + pos, htCap, sizeof(htCap));
        pos += sizeof(htCap);
    }

    // VHT Capabilities IE (ID=191, 12 bytes)
    if (vht && pos + 14 <= outCap) {
        out[pos++] = 191;
        out[pos++] = 12;
        uint8_t vhtCap[12] = {};
        // 1SS: Max MPDU 11454, RX LDPC, SGI80 (when 80MHz), RX STBC 1SS, NO TX STBC (BIT 7 = 0)
        uint32_t vhtCapInfo = (bw >= CMD_CBW_80MHZ) ? 0x03800132U : 0x03800112U;
        putLe32(vhtCap + 0, vhtCapInfo);
        putLe16(vhtCap + 4, 0xfffe);     // RX MCS 0-9 for 1SS
        putLe16(vhtCap + 8, 0xfffe);     // TX MCS 0-9 for 1SS
        memcpy(out + pos, vhtCap, sizeof(vhtCap));
        pos += sizeof(vhtCap);
    }

    // HE Capabilities Extension IE (ID=255, Ext=35, 21 bytes = 6 MAC + 11 PHY + 4 MCS <=80MHz)
    if (he && pos + 24 <= outCap) {
        out[pos++] = 255;
        out[pos++] = 22;
        out[pos++] = 35; // HE Capabilities
        uint8_t heCap[21] = {};
        heCap[0] = 0x05; // HTC + TWT requester
        heCap[3] = 0x18; // Max A-MPDU length exponent
        heCap[4] = 0x04; // A-MSDU in A-MPDU
        // Channel Width Set (must match our tuned hardware BW; NEVER set BIT(3) 160MHz without 160MHz MCS map!)
        if (fiveGhz && bw >= CMD_CBW_40MHZ)
            heCap[6] = 0x04; // 40/80 MHz in 5 GHz
        else if (!fiveGhz && bw >= CMD_CBW_40MHZ)
            heCap[6] = 0x02; // 40 MHz in 2.4 GHz
        else
            heCap[6] = 0x00; // 20 MHz only
        heCap[7] = 0x22; // LDPC coding in payload + 1x HE-LTF 0.8us GI
        heCap[8] = 0x42; // NDP 4x LTF 3.2us + RX STBC <= 80 MHz (1SS, no TX STBC)
        putLe16(heCap + 17, 0xfffe); // RX HE-MCS 0-11 1SS (<=80MHz)
        putLe16(heCap + 19, 0xfffe); // TX HE-MCS 0-11 1SS (<=80MHz)
        memcpy(out + pos, heCap, sizeof(heCap));
        pos += sizeof(heCap);
    }

    // WMM / QoS Info Vendor IE (OUI 00:50:f2, type 2, subtype 0)
    if (pos + 9 <= outCap) {
        static constexpr uint8_t kWmmIe[] = { 221, 7, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00 };
        memcpy(out + pos, kWmmIe, sizeof(kWmmIe));
        pos += sizeof(kWmmIe);
    }

    return pos;
}

size_t MT7902Sta::buildDeauthFrame(const uint8_t staMac[6], const uint8_t bssid[6],
                                   uint16_t seqCtrl, uint16_t reasonCode,
                                   uint8_t *out, size_t outCap) {
    if (!out || outCap < 26)
        return 0;
    memset(out, 0, 26);
    putLe16(out + 0, 0x00c0); // Deauthentication
    putLe16(out + 2, 0x013a);
    memcpy(out + 4, bssid, 6);
    memcpy(out + 10, staMac, 6);
    memcpy(out + 16, bssid, 6);
    putLe16(out + 22, seqCtrl);
    putLe16(out + 24, reasonCode);
    return 26;
}

size_t MT7902Sta::buildAddbaReqFrame(const uint8_t staMac[6], const uint8_t bssid[6],
                                     uint16_t seqCtrl, uint8_t dialogToken,
                                     uint8_t tid, uint16_t winSize, bool amsdu, uint16_t ssn,
                                     uint8_t *out, size_t outCap) {
    if (!out || outCap < 33)
        return 0;
    memset(out, 0, 33);
    putLe16(out + 0, 0x00d0); // Management Action
    putLe16(out + 2, 0x013a);
    memcpy(out + 4, bssid, 6);
    memcpy(out + 10, staMac, 6);
    memcpy(out + 16, bssid, 6);
    putLe16(out + 22, seqCtrl);
    out[24] = 3; // Category: Block Ack
    out[25] = 0; // Action: ADDBA Request
    out[26] = dialogToken ? dialogToken : 1;
    uint16_t baParams = static_cast<uint16_t>(
        (amsdu ? 1U : 0U) | (1U << 1) | ((tid & 0xfU) << 2) | ((winSize & 0x3ffU) << 6));
    putLe16(out + 27, baParams);
    putLe16(out + 29, 0); // BA timeout = 0 (disabled)
    putLe16(out + 31, static_cast<uint16_t>((ssn & 0x0fffU) << 4));
    return 33;
}

void MT7902Sta::deliverReorderedFrame(const uint8_t *ethFrame, size_t len, void *ctx) {
    auto *self = static_cast<MT7902Sta *>(ctx);
    if (self && self->mListener)
        self->mListener->onDataFrameReceived(ethFrame, len);
}

int MT7902Sta::initiateTxAddba(uint8_t tid) {
    if (!mPowerOn || mLink.state != kStaRun || !mLink.qos || tid >= 8)
        return kErrIO;
    mTxBaDialogToken = static_cast<uint8_t>(mTxBaDialogToken + 1U);
    if (mTxBaDialogToken == 0)
        mTxBaDialogToken = 1;
    mTxBaAttempts++;
    mTxBaLastReqMs = mDev.bus().nowMs();

    uint8_t req[33] = {};
    size_t reqLen = buildAddbaReqFrame(mDev.capability().macAddr, mLink.bssid,
                                       nextMgmtSeq(), mTxBaDialogToken,
                                       tid, 64, true, 0, req, sizeof(req));
    bool fiveGhz = (mLink.band != kBand2GHz);
    int err = mDev.send80211Frame(req, reqLen, mLink.apWcid, false, false, fiveGhz);
    MTK_LOG("Tx ADDBA Req: tid=%u win=64 amsdu=1 tok=%u -> err=%d",
            tid, mTxBaDialogToken, err);
    return err;
}

void MT7902Sta::pollRxReorderTimeouts() {
    if (!mRxReorder.hasPendingFrames())
        return;
    mRxReorder.checkTimeouts(mDev.bus().nowMs(), &MT7902Sta::deliverReorderedFrame, this);
}

void MT7902Sta::pollPeriodic() {
    pollRxReorderTimeouts();
    if (mPowerOn && mLink.state == kStaRun && mLink.qos &&
        !(mTxBaActiveMask & 1U) && mTxBaAttempts > 0 && mTxBaAttempts < 3) {
        uint64_t nowMs = mDev.bus().nowMs();
        if (nowMs >= mTxBaLastReqMs + 2000ULL)
            (void)initiateTxAddba(0);
    }
}

int MT7902Sta::associate(const AssocRequestParams &req) {
    if (!mPowerOn)
        return kErrIO;

    if (mScanning)
        (void)abortScan(false);

    // Find target AP in scan cache (prefer BSSID match, fall back to SSID)
    const ScannedBss *target = nullptr;
    if (req.hasBssid)
        target = mScanCache.findByBssid(req.bssid);
    if (!target && req.ssidLen > 0)
        target = mScanCache.findBySsid(req.ssid, req.ssidLen);

    if (mLink.state >= kStaAuthenticating || mStaAdded || mBssActive)
        (void)disassociate(8);

    mWpa2.reset();
    mRxReorder.resetAll();
    mAuthRetryCount = 0;
    mBaLoggedMask = 0;
    mTxBaActiveMask = 0;
    mTxBaAmsduMask = 0;
    mTxBaAttempts = 0;
    mTxBaLastReqMs = 0;
    mLink = {};
    mLink.authLower = req.authLower;
    mLink.authUpper = req.authUpper;
    mLink.encrypted = (req.authUpper != 0);
    if (req.rsnIeLen > 0 && req.rsnIeLen <= sizeof(mLink.rsnIe)) {
        memcpy(mLink.rsnIe, req.rsnIe, req.rsnIeLen);
        mLink.rsnIeLen = req.rsnIeLen;
    }

    if (target) {
        memcpy(mLink.bssid, target->bssid, 6);
        memcpy(mLink.ssid, target->ssid, target->ssidLen);
        mLink.ssidLen = target->ssidLen;
        mLink.channel = target->channel;
        mLink.band = target->band;
        mLink.apHasVht = target->hasVht;
        mLink.apHasHe = target->hasHe;
        mLink.bssColor = target->bssColor;

        // Validate operating channel width & center frequency from HT/VHT Operation IEs
        uint8_t candBw = target->bw;
        uint8_t candCenter = target->centerChannel ? target->centerChannel : target->channel;
        int diff = static_cast<int>(candCenter) - static_cast<int>(mLink.channel);
        if (diff < 0) diff = -diff;
        if (candBw == CMD_CBW_80MHZ && mLink.band == kBand5GHz &&
            candCenter >= 42 && candCenter <= 155 && diff <= 6) {
            mLink.bw = CMD_CBW_80MHZ;
            mLink.centerChannel = candCenter;
        } else if (candBw >= CMD_CBW_40MHZ && diff == 2) {
            mLink.bw = CMD_CBW_40MHZ;
            mLink.centerChannel = candCenter;
        } else {
            mLink.bw = CMD_CBW_20MHZ;
            mLink.centerChannel = mLink.channel;
        }
        mLink.beaconInterval = target->beaconInterval;
        mLink.dtimPeriod = target->dtimPeriod;
        mLink.capability = target->capability;
        mLink.qos = target->hasQos || target->hasHt || target->hasVht || target->hasHe;
        mLink.rssi = target->rssi;
        mLink.noise = target->noise;
        memcpy(mLink.apIes, target->ies, target->ieLen);
        mLink.apIeLen = target->ieLen;
    } else if (req.hasBssid) {
        memcpy(mLink.bssid, req.bssid, 6);
        memcpy(mLink.ssid, req.ssid, req.ssidLen);
        mLink.ssidLen = req.ssidLen;
        mLink.channel = req.channelHint ? req.channelHint : 1;
        mLink.centerChannel = mLink.channel;
        mLink.bw = CMD_CBW_20MHZ;
        mLink.band = (mLink.channel > 14) ? kBand5GHz : kBand2GHz;
    } else {
        MTK_ERR("Association target not found in scan cache");
        return kErrNotFound;
    }

    if (mLink.encrypted) {
        uint8_t grpCipher = MCU_CIPHER_AES_CCMP;
        if (mLink.apIeLen > 0 && (req.hasPmk || mLink.rsnIeLen == 0)) {
            uint8_t builtRsn[64] = {};
            size_t builtLen = MT7902Wpa2::buildStaRsnIeFromAp(
                mLink.apIes, mLink.apIeLen, builtRsn, grpCipher);
            if (builtLen > 0) {
                memcpy(mLink.rsnIe, builtRsn, builtLen);
                mLink.rsnIeLen = static_cast<uint16_t>(builtLen);
            }
        } else if (mLink.rsnIeLen >= 8 && mLink.rsnIe[0] == 48 && mLink.rsnIe[7] == 2) {
            grpCipher = MCU_CIPHER_TKIP;
        }
        if (req.hasPmk) {
            mWpa2.start(req.pmk, mDev.capability().macAddr, mLink.bssid,
                        mLink.rsnIe, mLink.rsnIeLen, grpCipher, mDev.bus().nowMs());
        }
    }

    // Tune MT7902 radio to AP channel & acquire join ROC channel privilege (matching mt7921_mgd_prepare_tx)
    ChannelConfig ch;
    ch.primaryChannel = mLink.channel;
    ch.centerChannel = mLink.channel;
    ch.band = static_cast<uint8_t>(mLink.band);
    ch.bw = CMD_CBW_20MHZ;
    int err = mDev.mcu().setChannel(ch, mDev.capability().antennaMask);
    if (err != kOk)
        return err;
    mDev.mac().setTiming(ch.band == 0, 9);
    (void)mDev.mcu().acquireRoc(0, ch, 5000, true /* MT7921_ROC_REQ_JOIN */);

    // Clear WTBL ADM counters & register peer STA + WTBL record (matching mt7921_mac_sta_add)
    mDev.mac().wtblUpdate(mLink.apWcid, MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
    StaUpdateParams staP;
    staP.bssIdx = 0;
    staP.wcid = mLink.apWcid;
    staP.aid = 0;
    memcpy(staP.peerMac, mLink.bssid, 6);
    staP.state = CONN_STATE_PORT_SECURE;
    staP.staState = MT76_STA_INFO_STATE_NONE;
    staP.band = static_cast<uint8_t>(mLink.band);
    staP.qos = mLink.qos;
    staP.ht = true;
    staP.enable = true;
    staP.newly = true;
    staP.rssi = static_cast<int8_t>(mLink.rssi);
    err = mDev.mcu().uniUpdateSta(staP);
    if (err != kOk) {
        MTK_ERR("uniUpdateSta (pre-auth wcid=%u) failed: %d", mLink.apWcid, err);
        (void)mDev.mcu().abortRoc(0);
        mLink.state = kStaInit;
        return err;
    }
    mStaAdded = true;

    // Send 802.11 Open System Authentication Request using peer WCID and band-appropriate basic rate
    uint8_t authFrame[64];
    size_t authLen = buildAuthReqFrame(mDev.capability().macAddr, mLink.bssid,
                                       nextMgmtSeq(), 0, 1, authFrame, sizeof(authFrame));
    mLink.state = kStaAuthenticating;

    bool fiveGhz = (mLink.band != kBand2GHz);
    err = mDev.send80211Frame(authFrame, authLen, mLink.apWcid, false, false, fiveGhz);
    MTK_LOG("Sent Auth Req -> BSSID %02x:%02x:%02x:%02x:%02x:%02x ch=%u band=%u wcid=%u err=%d",
            mLink.bssid[0], mLink.bssid[1], mLink.bssid[2],
            mLink.bssid[3], mLink.bssid[4], mLink.bssid[5],
            mLink.channel, static_cast<unsigned>(mLink.band), mLink.apWcid, err);
    if (err != kOk) {
        cleanupHardwareSession(false, 0);
        mLink.state = kStaInit;
        return err;
    }
    return kOk;
}

void MT7902Sta::cleanupHardwareSession(bool sendDeauth, uint16_t reasonCode) {
    if (!mPowerOn || mCleaningUp)
        return;
    mCleaningUp = true;

    if (sendDeauth) {
        uint8_t deauth[32];
        size_t len = buildDeauthFrame(mDev.capability().macAddr, mLink.bssid,
                                      nextMgmtSeq(), reasonCode, deauth, sizeof(deauth));
        bool fiveGhz = (mLink.band != kBand2GHz);
        if (mDev.send80211Frame(deauth, len, mLink.apWcid, false, false, fiveGhz) == kOk) {
            // Allow hardware TX queue to transmit Deauth over the air before removing WTBL/BSS,
            // preventing WPA2/WPA3 APs from retaining stale STA state and rejecting reconnects (status=30).
            mDev.bus().sleepMs(15);
        }
    }

    (void)mDev.mcu().abortRoc(0);

    if (mStaAdded) {
        StaUpdateParams staP;
        staP.bssIdx = 0;
        staP.wcid = mLink.apWcid;
        memcpy(staP.peerMac, mLink.bssid, 6);
        staP.enable = false;
        (void)mDev.mcu().uniUpdateSta(staP);
        mDev.mac().wtblUpdate(mLink.apWcid, MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
        mStaAdded = false;
    }

    if (mBssActive) {
        (void)mDev.mcu().setBssConnected(0, 0, 100, 1, false);

        StaUpdateParams bmcP;
        bmcP.bssIdx = 0;
        bmcP.wcid = MT7902_WTBL_SIZE - 1;
        memcpy(bmcP.peerMac, mLink.bssid, 6);
        bmcP.broadcast = true;
        bmcP.enable = false;
        (void)mDev.mcu().uniUpdateSta(bmcP);
        mDev.mac().wtblUpdate(MT7902_WTBL_SIZE - 1, MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

        ChannelConfig ch;
        ch.primaryChannel = mLink.channel;
        ch.centerChannel = mLink.channel;
        ch.band = static_cast<uint8_t>(mLink.band);
        (void)mDev.mcu().uniAddBss(0, 0, MT7902_WTBL_SIZE - 1, mLink.bssid, ch, 100, 1, false, false);
        mBssActive = false;
    }

    mCleaningUp = false;
}

int MT7902Sta::disassociate(uint16_t reasonCode) {
    bool wasLinked = (mLink.state >= kStaAssociated);
    bool wasActive = (mLink.state >= kStaAuthenticating);
    bool hadHw = (wasActive || mStaAdded || mBssActive);

    // Transition software state BEFORE MCU cleanup so re-entrant RX events cannot re-enter teardown
    mWpa2.reset();
    mRxReorder.resetAll();
    mTxBaActiveMask = 0;
    mTxBaAmsduMask = 0;
    mTxBaAttempts = 0;
    mLink.state = kStaInit;
    mLink.aid = 0;
    mLink.ptkInstalled = false;
    mLink.gtkInstalled = false;
    mLink.lastDeauthReason = reasonCode;

    if (mPowerOn && hadHw)
        cleanupHardwareSession(wasActive, reasonCode);

    if (wasLinked && mListener)
        mListener->onLinkStateChanged(false);

    return kOk;
}

int MT7902Sta::installCipherKey(uint8_t cipher, uint8_t keyIdx, const uint8_t *key,
                                size_t keyLen, bool pairwise, bool setKey) {
    if (!mPowerOn || mLink.state < kStaAssociated)
        return kErrIO;
    if (setKey && (!key || keyLen == 0 || keyLen > 32))
        return kErrInvalid;

    KeyInstallParams kp;
    kp.bssIdx = 0;
    kp.wcid = pairwise ? mLink.apWcid : static_cast<uint16_t>(MT7902_WTBL_SIZE - 1);
    kp.cipher = cipher;
    kp.keyIdx = keyIdx;
    kp.keyLen = static_cast<uint8_t>(keyLen);
    kp.setKey = setKey;
    if (setKey && key)
        memcpy(kp.key, key, keyLen);

    int err = mDev.mcu().uniSetKey(kp);
    MTK_LOG("installCipherKey(%s): cipher=%u idx=%u len=%zu wcid=%u set=%d -> err=%d",
            pairwise ? "PTK" : "GTK", cipher, keyIdx, keyLen, kp.wcid, setKey, err);
    if (err != kOk)
        return err;

    if (setKey) {
        if (pairwise) mLink.ptkInstalled = true;
        else mLink.gtkInstalled = true;

        if (mLink.ptkInstalled && mLink.state != kStaRun) {
            mLink.state = kStaRun;
            if (mListener)
                mListener->onLinkStateChanged(true);
        }
    }
    return kOk;
}

int MT7902Sta::outputEthernetPacket(const uint8_t *ethFrame, size_t len, bool kick) {
    if (!mPowerOn || !ethFrame || len < kEthHdrLen)
        return kErrInvalid;

    uint16_t ethType = getBe16(ethFrame + 12);
    bool isEapol = (ethType == 0x888e);

    if (mLink.state < kStaAssociated)
        return kErrIO;
    if (!isEapol && mLink.state != kStaRun)
        return kErrIO;

    bool unicast = (ethFrame[0] & 0x01U) == 0;
    if (!isEapol && unicast && mLink.qos && !(mTxBaActiveMask & 1U) && mTxBaAttempts < 3) {
        uint64_t nowMs = mDev.bus().nowMs();
        if (mTxBaAttempts == 0 || nowMs >= mTxBaLastReqMs + 2000ULL) {
            (void)initiateTxAddba(0);
        }
    }

    bool protect = mLink.encrypted && mLink.ptkInstalled && !isEapol;
    bool hwAmsdu = !isEapol && unicast && mLink.qos && ((mTxBaAmsduMask & 1U) != 0);
    return mDev.sendEthernetFrame(ethFrame, len, mLink.apWcid, mLink.qos, protect, hwAmsdu, kick);
}

void MT7902Sta::handleAuthResp(const uint8_t *frame, size_t len) {
    if (mLink.state != kStaAuthenticating || len < 30)
        return;
    if (memcmp(frame + 16, mLink.bssid, 6) != 0)
        return;

    uint16_t status = getLe16(frame + 28);
    if (status == 30 && mAuthRetryCount == 0) {
        // IEEE 802.11w (PMF) AP may return status=30 (REJECTED_TEMPORARILY) if it still holds
        // a stale STA session; wait for comeback interval and retry Open System Auth once.
        uint32_t comebackMs = 1050;
        size_t pos = 30;
        while (pos + 2 <= len) {
            uint8_t eid = frame[pos];
            uint8_t elen = frame[pos + 1];
            if (pos + 2 + elen > len) break;
            if (eid == 56 && elen >= 5 && frame[pos + 2] == 3) {
                uint32_t tus = getLe32(frame + pos + 3);
                comebackMs = tus + 50;
                if (comebackMs < 200) comebackMs = 200;
                if (comebackMs > 1200) comebackMs = 1200;
                break;
            }
            pos += 2 + elen;
        }
        mAuthRetryCount++;
        MTK_LOG("802.11 Auth temporarily rejected (status=30); retrying after %ums", comebackMs);
        mDev.bus().sleepMs(comebackMs);
        bool fiveGhz = (mLink.band != kBand2GHz);
        uint8_t authFrame[64];
        size_t authLen = buildAuthReqFrame(mDev.capability().macAddr, mLink.bssid,
                                           nextMgmtSeq(), 0, 1, authFrame, sizeof(authFrame));
        if (mDev.send80211Frame(authFrame, authLen, mLink.apWcid, false, false, fiveGhz) == kOk)
            return;
    }
    if (status != 0) {
        MTK_ERR("802.11 Authentication rejected (status=%u)", status);
        mLink.lastAssocStatus = status;
        mLink.state = kStaInit;
        cleanupHardwareSession(false, 0);
        if (mListener)
            mListener->onAssociationCompleted(false, status);
        return;
    }

    bool fiveGhz = (mLink.band != kBand2GHz);
    uint8_t assocFrame[512];
    size_t assocLen = buildAssocReqFrame(
        mDev.capability().macAddr, mLink.bssid, nextMgmtSeq(),
        mLink.ssid, mLink.ssidLen,
        mLink.rsnIeLen ? mLink.rsnIe : nullptr, mLink.rsnIeLen,
        true, fiveGhz, mDev.capability().supportsHe,
        assocFrame, sizeof(assocFrame), fiveGhz, mLink.bw);

    mLink.state = kStaAssociating;
    int txErr = mDev.send80211Frame(assocFrame, assocLen, mLink.apWcid, false, false, fiveGhz);
    MTK_LOG("Rx Auth Resp OK -> Sent Assoc Req (%zu bytes, wcid=%u, 5G=%d, err=%d)",
            assocLen, mLink.apWcid, fiveGhz, txErr);
    if (txErr != kOk) {
        mLink.state = kStaInit;
        cleanupHardwareSession(false, 0);
        if (mListener)
            mListener->onAssociationCompleted(false, 1);
    }
}

void MT7902Sta::handleAssocResp(const uint8_t *frame, size_t len) {
    if (mLink.state != kStaAssociating || len < 30)
        return;
    if (memcmp(frame + 16, mLink.bssid, 6) != 0)
        return;

    uint16_t status = getLe16(frame + 26);
    uint16_t aid = getLe16(frame + 28) & 0x3fffU;
    mLink.lastAssocStatus = status;

    if (status != 0) {
        MTK_ERR("802.11 Association rejected (status=%u)", status);
        mLink.state = kStaInit;
        cleanupHardwareSession(false, 0);
        if (mListener)
            mListener->onAssociationCompleted(false, status);
        return;
    }

    mLink.aid = aid;
    mLink.state = mLink.encrypted ? kStaAssociated : kStaRun;

    // Release 20 MHz JOIN ROC BEFORE configuring full operating bandwidth (matching mt7921_mgd_complete_tx)
    (void)mDev.mcu().abortRoc(0);

    // Program MT7902 hardware channel, SIFS/slot timing, BSS, EDCA, BMC WTBL, and peer STA record
    ChannelConfig ch;
    ch.primaryChannel = mLink.channel;
    ch.centerChannel = mLink.centerChannel ? mLink.centerChannel : mLink.channel;
    ch.band = static_cast<uint8_t>(mLink.band);
    ch.bw = mLink.bw;

    constexpr uint16_t kBmcWcid = MT7902_WTBL_SIZE - 1;
    bool useVht = (mLink.band != kBand2GHz) && mLink.apHasVht;
    bool useHe = mDev.capability().supportsHe && mLink.apHasHe;
    (void)mDev.mcu().setChannel(ch, mDev.capability().antennaMask);
    mDev.mac().setTiming(ch.band == 0, 9);
    int bssErr = mDev.mcu().uniAddBss(0, 0, kBmcWcid, mLink.bssid, ch,
                                      mLink.beaconInterval, mLink.dtimPeriod,
                                      mLink.qos, true, useHe, mLink.bssColor);
    if (bssErr == kOk)
        mBssActive = true;
    (void)mDev.mcu().setEdcaParams(0, mLink.qos);

    mDev.mac().wtblUpdate(kBmcWcid, MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
    StaUpdateParams bmcP;
    bmcP.bssIdx = 0;
    bmcP.wcid = kBmcWcid;
    bmcP.aid = aid;
    memcpy(bmcP.peerMac, mLink.bssid, 6);
    bmcP.state = CONN_STATE_PORT_SECURE;
    bmcP.staState = MT76_STA_INFO_STATE_ASSOC;
    bmcP.band = static_cast<uint8_t>(mLink.band);
    bmcP.bw = mLink.bw;
    bmcP.qos = false;
    bmcP.ht = false;
    bmcP.enable = true;
    bmcP.newly = true;
    bmcP.broadcast = true;
    bmcP.rssi = static_cast<int8_t>(mLink.rssi);
    (void)mDev.mcu().uniUpdateSta(bmcP);

    mDev.mac().wtblUpdate(mLink.apWcid, MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
    StaUpdateParams staP;
    staP.bssIdx = 0;
    staP.wcid = mLink.apWcid;
    staP.aid = aid;
    memcpy(staP.peerMac, mLink.bssid, 6);
    staP.state = CONN_STATE_PORT_SECURE;
    staP.staState = MT76_STA_INFO_STATE_ASSOC;
    staP.band = static_cast<uint8_t>(mLink.band);
    staP.bw = mLink.bw;
    staP.qos = mLink.qos;
    staP.ht = true;
    staP.vht = useVht;
    staP.he = useHe;
    staP.enable = true;
    staP.newly = false;
    staP.rssi = static_cast<int8_t>(mLink.rssi);
    (void)mDev.mcu().uniUpdateSta(staP);
    (void)mDev.mcu().setBssConnected(0, aid, mLink.beaconInterval, mLink.dtimPeriod, true);

    MTK_LOG("Associated to BSSID %02x:%02x:%02x:%02x:%02x:%02x ch=%u (center=%u bw=%u) aid=%u encrypted=%d bssErr=%d",
            mLink.bssid[0], mLink.bssid[1], mLink.bssid[2],
            mLink.bssid[3], mLink.bssid[4], mLink.bssid[5],
            mLink.channel, ch.centerChannel, ch.bw, aid, mLink.encrypted, bssErr);

    if (mListener) {
        mListener->onAssociationCompleted(true, 0);
        if (!mLink.encrypted)
            mListener->onLinkStateChanged(true);
    }
}

void MT7902Sta::handleDeauthOrDisassoc(const uint8_t *frame, size_t len, bool isDisassoc) {
    if (mLink.state < kStaAuthenticating || len < 26)
        return;
    if (memcmp(frame + 16, mLink.bssid, 6) != 0)
        return;

    uint16_t reason = getLe16(frame + 24);
    MTK_LOG("Rx %s from AP (reason=%u)", isDisassoc ? "Disassoc" : "Deauth", reason);
    mLink.lastDeauthReason = reason;
    bool wasLinked = (mLink.state >= kStaAssociated);
    // Clear state BEFORE MCU teardown so duplicate Deauth frames cannot re-enter cleanup
    mWpa2.reset();
    mRxReorder.resetAll();
    mTxBaActiveMask = 0;
    mTxBaAmsduMask = 0;
    mTxBaAttempts = 0;
    mLink.state = kStaInit;
    mLink.aid = 0;
    mLink.ptkInstalled = false;
    mLink.gtkInstalled = false;
    cleanupHardwareSession(false, 0);

    if (mListener) {
        if (wasLinked)
            mListener->onLinkStateChanged(false);
        mListener->onDeauthenticated(reason, isDisassoc);
    }
}

void MT7902Sta::handleActionFrame(const uint8_t *frame, size_t len) {
    if (mLink.state < kStaAssociated || len < 26)
        return;
    if (memcmp(frame + 16, mLink.bssid, 6) != 0)
        return;

    uint8_t category = frame[24];
    uint8_t action = frame[25];
    if (category != 3) // Block Ack
        return;

    if (action == 0 && len >= 33) { // ADDBA Request -> accept RX Block Ack for A-MPDU
        uint8_t dialogToken = frame[26];
        uint16_t baParams = getLe16(frame + 27);
        uint16_t baTimeout = getLe16(frame + 29);
        uint16_t startSeq = getLe16(frame + 31);

        bool amsdu = (baParams & 0x0001U) != 0;
        uint8_t tid = static_cast<uint8_t>((baParams >> 2) & 0x0fU);
        uint16_t reqWin = static_cast<uint16_t>((baParams >> 6) & 0x03ffU);
        uint16_t winSize = (reqWin == 0 || reqWin > 64) ? 64 : reqWin;
        uint16_t ssn = static_cast<uint16_t>((startSeq >> 4) & 0x0fffU);

        // Start software IEEE 802.11 RX A-MPDU reorder buffer (matching Linux mt7921_ampdu_action)
        (void)mRxReorder.startSession(tid, ssn, winSize, &MT7902Sta::deliverReorderedFrame, this);

        int baErr = mDev.mcu().uniSetBa(0, mLink.apWcid, mLink.bssid, tid, ssn, winSize, amsdu, true, false);
        uint8_t tidBit = static_cast<uint8_t>(1U << (tid & 0x7U));
        if ((mBaLoggedMask & tidBit) == 0) {
            mBaLoggedMask |= tidBit;
            MTK_LOG("Rx ADDBA Req: tid=%u win=%u ssn=%u amsdu=%d -> err=%d",
                    tid, winSize, ssn, amsdu, baErr);
        }

        // Send 802.11 ADDBA Response (non-robust Action frame, protect=false)
        uint8_t resp[33] = {};
        putLe16(resp + 0, 0x00d0); // Management Action
        putLe16(resp + 2, 0x013a);
        memcpy(resp + 4, mLink.bssid, 6);
        memcpy(resp + 10, mDev.capability().macAddr, 6);
        memcpy(resp + 16, mLink.bssid, 6);
        putLe16(resp + 22, nextMgmtSeq());
        resp[24] = 3;           // Block Ack
        resp[25] = 1;           // ADDBA Response
        resp[26] = dialogToken;
        putLe16(resp + 27, 0);  // Status = Success
        uint16_t respParams = static_cast<uint16_t>(
            (amsdu ? 1U : 0U) | (1U << 1) | ((tid & 0xfU) << 2) | ((winSize & 0x3ffU) << 6));
        putLe16(resp + 29, respParams);
        putLe16(resp + 31, baTimeout);

        bool fiveGhz = (mLink.band != kBand2GHz);
        (void)mDev.send80211Frame(resp, sizeof(resp), mLink.apWcid, false, false, fiveGhz);
    } else if (action == 1 && len >= 33) { // ADDBA Response -> activate hardware TX Block Ack
        uint16_t status = getLe16(frame + 27);
        uint16_t baParams = getLe16(frame + 29);
        bool amsdu = (baParams & 0x0001U) != 0;
        uint8_t tid = static_cast<uint8_t>((baParams >> 2) & 0x0fU);
        uint16_t reqWin = static_cast<uint16_t>((baParams >> 6) & 0x03ffU);
        uint16_t winSize = (reqWin == 0 || reqWin > 64) ? 64 : reqWin;
        if (status == 0 && tid < 8) {
            int baErr = mDev.mcu().uniSetBa(0, mLink.apWcid, mLink.bssid, tid, 0, winSize,
                                            amsdu, true, true /* tx */);
            if (baErr == kOk) {
                uint8_t tidBit = static_cast<uint8_t>(1U << tid);
                mTxBaActiveMask |= tidBit;
                if (amsdu)
                    mTxBaAmsduMask |= tidBit;
                else
                    mTxBaAmsduMask &= static_cast<uint8_t>(~tidBit);
            }
            MTK_LOG("Rx ADDBA Resp OK: tid=%u win=%u amsdu=%d -> TX BA err=%d",
                    tid, winSize, amsdu, baErr);
        } else {
            MTK_LOG("Rx ADDBA Resp declined: tid=%u status=%u", tid, status);
        }
    } else if (action == 2 && len >= 30) { // DELBA
        uint16_t delbaParams = getLe16(frame + 26);
        uint8_t tid = static_cast<uint8_t>((delbaParams >> 12) & 0x0fU);
        bool initiator = ((delbaParams >> 11) & 0x1U) != 0;
        if (initiator) {
            mRxReorder.stopSession(tid, &MT7902Sta::deliverReorderedFrame, this);
            (void)mDev.mcu().uniSetBa(0, mLink.apWcid, mLink.bssid, tid, 0, 0, false, false, false);
        } else {
            if (tid < 8) {
                uint8_t tidBit = static_cast<uint8_t>(1U << tid);
                mTxBaActiveMask &= static_cast<uint8_t>(~tidBit);
                mTxBaAmsduMask &= static_cast<uint8_t>(~tidBit);
            }
            (void)mDev.mcu().uniSetBa(0, mLink.apWcid, mLink.bssid, tid, 0, 0, false, false, true);
        }
    }
}

bool MT7902Sta::handleInboundEapol(const uint8_t *ethFrame, size_t len) {
    if (!ethFrame || len < kEthHdrLen + 4 || getBe16(ethFrame + 12) != 0x888e)
        return false;
    if (!mWpa2.active || mLink.state < kStaAssociated)
        return false;

    uint8_t txReply[512] = {};
    size_t txLen = 0;
    Wpa2GtkResult gtk = {};
    Wpa2StepResult res = mWpa2.processRxEapol(ethFrame, len, txReply, sizeof(txReply), txLen, gtk);

    if (res == kWpa2SentMsg2 && txLen > 0) {
        MTK_LOG("WPA2 4-way handshake: Rx Msg 1/4 -> Tx Msg 2/4 (%zu bytes)", txLen);
        (void)mDev.sendEthernetFrame(txReply, txLen, mLink.apWcid, mLink.qos, false);
    } else if (res == kWpa2Completed4Way && txLen > 0) {
        MTK_LOG("WPA2 4-way handshake: Rx Msg 3/4 -> Tx Msg 4/4 & installing PTK/GTK");
        (void)mDev.sendEthernetFrame(txReply, txLen, mLink.apWcid, mLink.qos, false);
        if (gtk.found && gtk.keyLen > 0)
            (void)installCipherKey(mWpa2.groupCipher, gtk.keyIdx, gtk.key, gtk.keyLen, false, true);
        (void)installCipherKey(MCU_CIPHER_AES_CCMP, 0, mWpa2.ptk.tk, 16, true, true);
    } else if (res == kWpa2CompletedGroupRekey && txLen > 0) {
        MTK_LOG("WPA2 group rekey: Tx Group Msg 2/2 & installing GTK idx=%u", gtk.keyIdx);
        (void)mDev.sendEthernetFrame(txReply, txLen, mLink.apWcid, mLink.qos, mLink.ptkInstalled);
        if (gtk.found && gtk.keyLen > 0)
            (void)installCipherKey(mWpa2.groupCipher, gtk.keyIdx, gtk.key, gtk.keyLen, false, true);
    } else if (res == kWpa2Error) {
        MTK_ERR("WPA2 EAPOL processing error (len=%zu)", len);
    }
    return true;
}

void MT7902Sta::handleData80211Frame(const uint8_t *frame, size_t len) {
    if (len < 32)
        return;
    uint16_t fc = getLe16(frame);
    uint8_t stype = (fc >> 4) & 0xfU;
    size_t hdrLen = 24;
    if (stype & 0x08U)
        hdrLen += 2; // QoS control
    if (len < hdrLen + 8)
        return;

    // Check LLC/SNAP header (aa aa 03 00 00 00 <ethType>)
    const uint8_t *llc = frame + hdrLen;
    if (llc[0] != 0xaa || llc[1] != 0xaa || llc[2] != 0x03)
        return;

    size_t payloadLen = len - hdrLen - 8;
    if (payloadLen + kEthHdrLen > 2048)
        return;

    uint8_t eth[2048];
    // FromDS=1: Addr1=DA, Addr3=SA
    memcpy(eth + 0, frame + 4, 6);
    memcpy(eth + 6, frame + 16, 6);
    eth[12] = llc[6];
    eth[13] = llc[7];
    if (payloadLen > 0)
        memcpy(eth + 14, llc + 8, payloadLen);

    if (handleInboundEapol(eth, kEthHdrLen + payloadLen))
        return;

    if (mListener)
        mListener->onDataFrameReceived(eth, kEthHdrLen + payloadLen);
}

void MT7902Sta::onRxBatchBegin() {
    if (mListener)
        mListener->onRxBatchBegin();
}

void MT7902Sta::onRxBatchComplete() {
    pollRxReorderTimeouts();
    if (mListener)
        mListener->onRxBatchComplete();
}

void MT7902Sta::onRxEthernetFrame(const uint8_t *ethFrame, size_t len, const ParsedRxFrame &meta) {
    if (meta.rssi < 0 && meta.rssi > -120) {
        mLink.rssi = meta.rssi;
        if (meta.rateValid) {
            bool isHighRate = (meta.phyMode >= MT_PHY_TYPE_HT);
            if (isHighRate || (meta.unicast && (len >= 100 || mLink.phyMode < MT_PHY_TYPE_HT))) {
                mLink.phyMode = meta.phyMode;
                mLink.mcs = meta.mcs;
                mLink.nss = meta.nss;
                mLink.txRateMbps = MT7902Mac::estimateRateMbps(meta.phyMode, meta.mcs, meta.nss, meta.bw);
            }
        }
    }
    if (handleInboundEapol(ethFrame, len))
        return;

    if (meta.aggr && len >= kEthHdrLen && (ethFrame[0] & 0x01U) == 0 &&
        mRxReorder.isSessionActive(meta.tid)) {
        mRxReorder.processFrame(ethFrame, len, meta, mDev.bus().nowMs(),
                                &MT7902Sta::deliverReorderedFrame, this);
        return;
    }

    if (mListener)
        mListener->onDataFrameReceived(ethFrame, len);
}

void MT7902Sta::onRx80211Frame(const uint8_t *dot11Frame, size_t len, const ParsedRxFrame &meta) {
    if (!dot11Frame || len < 20)
        return;

    uint16_t fc = getLe16(dot11Frame);
    uint8_t type = (fc >> 2) & 0x3U;
    uint8_t stype = (fc >> 4) & 0xfU;

    if (type == 1 && stype == 8) { // Control: Block Ack Request (BAR)
        if (mLink.state >= kStaAssociated && memcmp(dot11Frame + 10, mLink.bssid, 6) == 0) {
            uint16_t barCtrl = getLe16(dot11Frame + 16);
            uint8_t tid = static_cast<uint8_t>((barCtrl >> 12) & 0x0fU);
            uint16_t ssn = static_cast<uint16_t>((getLe16(dot11Frame + 18) >> 4) & 0x0fffU);
            mRxReorder.handleBar(tid, ssn, &MT7902Sta::deliverReorderedFrame, this);
        }
        return;
    }
    if (len < 24)
        return;

    if (type == 0) { // Management
        switch (stype) {
        case 8: // Beacon
        case 5: { // Probe Response
            ScannedBss bss;
            if (MT7902ScanCache::parseBeaconOrProbeResp(
                    dot11Frame, len, meta.rssi, meta.chFreq, mDev.bus().nowMs(), bss)) {
                mScanCache.upsert(bss);
                if (mListener) {
                    if (mScanning)
                        mListener->onBssDiscovered(bss);
                    if (mLink.state >= kStaAssociated &&
                        memcmp(bss.bssid, mLink.bssid, 6) == 0) {
                        mLink.rssi = bss.rssi;
                        mListener->onAssociatedBeacon(bss);
                    }
                }
            }
            break;
        }
        case 11: // Authentication
            handleAuthResp(dot11Frame, len);
            break;
        case 1: // Association Response
        case 3: // Reassociation Response
            handleAssocResp(dot11Frame, len);
            break;
        case 12: // Deauthentication
            handleDeauthOrDisassoc(dot11Frame, len, false);
            break;
        case 10: // Disassociation
            handleDeauthOrDisassoc(dot11Frame, len, true);
            break;
        case 13: // Action (Block Ack ADDBA/DELBA)
            handleActionFrame(dot11Frame, len);
            break;
        default:
            break;
        }
    } else if (type == 2) { // Raw 802.11 Data (e.g. EAPOL frames bypassing header translation)
        handleData80211Frame(dot11Frame, len);
    }
}

void MT7902Sta::onScanComplete(uint8_t seqNum) {
    (void)seqNum;
    mScanning = false;
    if (mLink.state == kStaScanning)
        mLink.state = kStaInit;
    restoreOperatingChannel();
    mScanCache.resetCursor();
    MTK_LOG("Scan finished: %zu BSS entries in cache", mScanCache.count());
    if (mListener)
        mListener->onScanFinished();
}

void MT7902Sta::onBeaconLoss(uint8_t bssIdx, uint8_t reason) {
    (void)bssIdx;
    MTK_LOG("Beacon loss reported by MT7902 firmware (reason=%u)", reason);
    if (mLink.state >= kStaAssociated) {
        (void)disassociate(4 /* DISASSOC_INACTIVE */);
        if (mListener)
            mListener->onDeauthenticated(4, true);
    }
}

void MT7902Sta::onFirmwareResetNeeded() {
    (void)mDev.resetHardware();
}

} // namespace mt7902
