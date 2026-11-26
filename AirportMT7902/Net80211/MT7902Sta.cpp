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
        if (mLink.state >= kStaAuthenticating)
            (void)disassociate(8);
        mScanning = false;
        int err = mDev.stopRadio();
        mPowerOn = false;
        mLink.state = kStaInit;
        return err;
    }
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

    int err = mDev.mcu().startHwScan(req);
    if (err == kOk) {
        mScanning = true;
        if (mLink.state == kStaInit)
            mLink.state = kStaScanning;
    }
    return err;
}

int MT7902Sta::abortScan() {
    if (!mScanning)
        return kOk;
    mScanning = false;
    if (mLink.state == kStaScanning)
        mLink.state = kStaInit;
    return mDev.mcu().cancelHwScan(0);
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
                                     uint8_t *out, size_t outCap) {
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
    uint16_t capab = 0x0421;
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

    // Supported Rates IE (ID=1): 1, 2, 5.5, 11, 6, 9, 12, 18 Mbps
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
        putLe16(htCap + 0, 0x01ef); // LDPC + 40MHz + SGI20/40 + Rx STBC
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
        putLe32(vhtCap + 0, 0x038001b2); // Max MPDU 11454, SGI80, RX LDPC, SU BFee
        putLe16(vhtCap + 4, 0xfffe);     // RX MCS 0-9 for 1SS
        putLe16(vhtCap + 8, 0xfffe);     // TX MCS 0-9 for 1SS
        memcpy(out + pos, vhtCap, sizeof(vhtCap));
        pos += sizeof(vhtCap);
    }

    // HE Capabilities Extension IE (ID=255, Ext=35)
    if (he && pos + 24 <= outCap) {
        out[pos++] = 255;
        out[pos++] = 22;
        out[pos++] = 35; // HE Capabilities
        uint8_t heCap[21] = {};
        heCap[0] = 0x05; // HTC + TWT requester
        heCap[6] = 0x0e; // 40MHz 2.4G + 40/80MHz 5G + 160MHz 5G
        heCap[7] = 0x02; // LDPC coding
        putLe16(heCap + 17, 0xfffe); // RX HE-MCS 0-11 1SS
        putLe16(heCap + 19, 0xfffe); // TX HE-MCS 0-11 1SS
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

int MT7902Sta::associate(const AssocRequestParams &req) {
    if (!mPowerOn)
        return kErrIO;

    if (mScanning)
        (void)abortScan();

    // Find target AP in scan cache (prefer BSSID match, fall back to SSID)
    const ScannedBss *target = nullptr;
    if (req.hasBssid)
        target = mScanCache.findByBssid(req.bssid);
    if (!target && req.ssidLen > 0)
        target = mScanCache.findBySsid(req.ssid, req.ssidLen);

    if (mLink.state >= kStaAuthenticating)
        (void)disassociate(8);

    mWpa2.reset();
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
        mLink.bw = target->bw;
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

    // Tune MT7902 radio to AP channel
    ChannelConfig ch;
    ch.primaryChannel = mLink.channel;
    ch.centerChannel = mLink.channel;
    ch.band = static_cast<uint8_t>(mLink.band);
    ch.bw = CMD_CBW_20MHZ; // Start in 20 MHz during auth/assoc, widen after assoc
    int err = mDev.mcu().setChannel(ch, mDev.capability().antennaMask);
    if (err != kOk)
        return err;

    // Register peer STA record in MT7902 WTBL for management exchange
    StaUpdateParams staP;
    staP.bssIdx = 0;
    staP.wcid = mLink.apWcid;
    staP.aid = 0;
    memcpy(staP.peerMac, mLink.bssid, 6);
    staP.state = CONN_STATE_CONNECT;
    staP.qos = mLink.qos;
    staP.enable = true;
    staP.newly = true;
    staP.rssi = static_cast<int8_t>(mLink.rssi);
    (void)mDev.mcu().uniUpdateSta(staP);

    // Send 802.11 Open System Authentication Request
    uint8_t authFrame[64];
    size_t authLen = buildAuthReqFrame(mDev.capability().macAddr, mLink.bssid,
                                       nextMgmtSeq(), 0, 1, authFrame, sizeof(authFrame));
    mLink.state = kStaAuthenticating;

    err = mDev.send80211Frame(authFrame, authLen, 0, false);
    if (err != kOk) {
        mLink.state = kStaInit;
        return err;
    }
    return kOk;
}

int MT7902Sta::disassociate(uint16_t reasonCode) {
    bool wasLinked = (mLink.state >= kStaAssociated);

    if (mPowerOn && mLink.state >= kStaAuthenticating) {
        uint8_t deauth[32];
        size_t len = buildDeauthFrame(mDev.capability().macAddr, mLink.bssid,
                                      nextMgmtSeq(), reasonCode, deauth, sizeof(deauth));
        (void)mDev.send80211Frame(deauth, len, 0, false);

        (void)mDev.mcu().setBssConnected(0, 0, 100, 1, false);

        StaUpdateParams staP;
        staP.bssIdx = 0;
        staP.wcid = mLink.apWcid;
        memcpy(staP.peerMac, mLink.bssid, 6);
        staP.enable = false;
        (void)mDev.mcu().uniUpdateSta(staP);

        StaUpdateParams bmcP;
        bmcP.bssIdx = 0;
        bmcP.wcid = MT7902_WTBL_SIZE - 1;
        memcpy(bmcP.peerMac, mLink.bssid, 6);
        bmcP.broadcast = true;
        bmcP.enable = false;
        (void)mDev.mcu().uniUpdateSta(bmcP);

        ChannelConfig ch;
        ch.primaryChannel = mLink.channel;
        ch.centerChannel = mLink.channel;
        ch.band = static_cast<uint8_t>(mLink.band);
        (void)mDev.mcu().uniAddBss(0, 0, MT7902_WTBL_SIZE - 1, mLink.bssid, ch, 100, 1, false, false);
    }

    mWpa2.reset();
    mLink.state = kStaInit;
    mLink.aid = 0;
    mLink.ptkInstalled = false;
    mLink.gtkInstalled = false;
    mLink.lastDeauthReason = reasonCode;

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

int MT7902Sta::outputEthernetPacket(const uint8_t *ethFrame, size_t len) {
    if (!mPowerOn || !ethFrame || len < kEthHdrLen)
        return kErrInvalid;

    uint16_t ethType = getBe16(ethFrame + 12);
    bool isEapol = (ethType == 0x888e);

    if (mLink.state < kStaAssociated)
        return kErrIO;
    if (!isEapol && mLink.state != kStaRun)
        return kErrIO;

    bool protect = mLink.encrypted && mLink.ptkInstalled && !isEapol;
    return mDev.sendEthernetFrame(ethFrame, len, mLink.apWcid, mLink.qos, protect);
}

void MT7902Sta::handleAuthResp(const uint8_t *frame, size_t len) {
    if (mLink.state != kStaAuthenticating || len < 30)
        return;
    if (memcmp(frame + 16, mLink.bssid, 6) != 0)
        return;

    uint16_t status = getLe16(frame + 28);
    if (status != 0) {
        MTK_ERR("802.11 Authentication rejected (status=%u)", status);
        mLink.lastAssocStatus = status;
        mLink.state = kStaInit;
        if (mListener)
            mListener->onAssociationCompleted(false, status);
        return;
    }

    uint8_t assocFrame[512];
    size_t assocLen = buildAssocReqFrame(
        mDev.capability().macAddr, mLink.bssid, nextMgmtSeq(),
        mLink.ssid, mLink.ssidLen,
        mLink.rsnIeLen ? mLink.rsnIe : nullptr, mLink.rsnIeLen,
        true, mLink.band != kBand2GHz, mDev.capability().supportsHe,
        assocFrame, sizeof(assocFrame));

    mLink.state = kStaAssociating;
    if (mDev.send80211Frame(assocFrame, assocLen, 0, false) != kOk) {
        mLink.state = kStaInit;
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
        if (mListener)
            mListener->onAssociationCompleted(false, status);
        return;
    }

    mLink.aid = aid;
    mLink.state = mLink.encrypted ? kStaAssociated : kStaRun;

    // Program MT7902 hardware BSS & STA state with full channel width
    ChannelConfig ch;
    ch.primaryChannel = mLink.channel;
    ch.centerChannel = mLink.channel;
    ch.band = static_cast<uint8_t>(mLink.band);
    ch.bw = mLink.bw;

    constexpr uint16_t kBmcWcid = MT7902_WTBL_SIZE - 1;
    (void)mDev.mcu().setChannel(ch, mDev.capability().antennaMask);
    (void)mDev.mcu().uniAddBss(0, 0, kBmcWcid, mLink.bssid, ch,
                               mLink.beaconInterval, mLink.dtimPeriod,
                               mLink.qos, true);

    StaUpdateParams bmcP;
    bmcP.bssIdx = 0;
    bmcP.wcid = kBmcWcid;
    bmcP.aid = aid;
    memcpy(bmcP.peerMac, mLink.bssid, 6);
    bmcP.state = CONN_STATE_PORT_SECURE;
    bmcP.qos = false;
    bmcP.enable = true;
    bmcP.newly = true;
    bmcP.broadcast = true;
    bmcP.rssi = static_cast<int8_t>(mLink.rssi);
    (void)mDev.mcu().uniUpdateSta(bmcP);

    StaUpdateParams staP;
    staP.bssIdx = 0;
    staP.wcid = mLink.apWcid;
    staP.aid = aid;
    memcpy(staP.peerMac, mLink.bssid, 6);
    staP.state = CONN_STATE_PORT_SECURE;
    staP.qos = mLink.qos;
    staP.enable = true;
    staP.newly = false;
    staP.rssi = static_cast<int8_t>(mLink.rssi);
    (void)mDev.mcu().uniUpdateSta(staP);
    (void)mDev.mcu().setBssConnected(0, aid, mLink.beaconInterval, mLink.dtimPeriod, true);

    MTK_LOG("Associated to BSSID %02x:%02x:%02x:%02x:%02x:%02x ch=%u aid=%u encrypted=%d",
            mLink.bssid[0], mLink.bssid[1], mLink.bssid[2],
            mLink.bssid[3], mLink.bssid[4], mLink.bssid[5],
            mLink.channel, aid, mLink.encrypted);

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
    mLink.lastDeauthReason = reason;
    bool wasLinked = (mLink.state >= kStaAssociated);
    mWpa2.reset();
    mLink.state = kStaInit;
    mLink.ptkInstalled = false;
    mLink.gtkInstalled = false;

    if (mListener) {
        if (wasLinked)
            mListener->onLinkStateChanged(false);
        mListener->onDeauthenticated(reason, isDisassoc);
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

void MT7902Sta::onRxEthernetFrame(const uint8_t *ethFrame, size_t len, const ParsedRxFrame &meta) {
    if (meta.rssi < 0 && meta.rssi > -120) {
        mLink.rssi = meta.rssi;
        mLink.phyMode = meta.phyMode;
        mLink.mcs = meta.mcs;
        mLink.nss = meta.nss;
        mLink.txRateMbps = MT7902Mac::estimateRateMbps(meta.phyMode, meta.mcs, meta.nss, meta.bw);
    }
    if (handleInboundEapol(ethFrame, len))
        return;
    if (mListener)
        mListener->onDataFrameReceived(ethFrame, len);
}

void MT7902Sta::onRx80211Frame(const uint8_t *dot11Frame, size_t len, const ParsedRxFrame &meta) {
    if (!dot11Frame || len < 24)
        return;

    uint16_t fc = getLe16(dot11Frame);
    uint8_t type = (fc >> 2) & 0x3U;
    uint8_t stype = (fc >> 4) & 0xfU;

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
    mScanCache.resetCursor();
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
