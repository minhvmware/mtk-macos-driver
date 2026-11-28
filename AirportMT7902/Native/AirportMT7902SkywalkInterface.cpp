// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#if defined(KERNEL) && defined(IO80211FAMILY_V2)

#include "AirportMT7902SkywalkInterface.hpp"
#include "AirportMT7902ControllerV2.hpp"

#define super IO80211InfraProtocol
OSDefineMetaClassAndStructors(AirportMT7902SkywalkInterface, IO80211InfraProtocol);

static uint8_t mapSkywalkCipher(uint32_t cipher) {
    switch (cipher) {
    case APPLE80211_CIPHER_WEP_40:  return mt7902::MCU_CIPHER_WEP40;
    case APPLE80211_CIPHER_WEP_104: return mt7902::MCU_CIPHER_WEP104;
    case APPLE80211_CIPHER_TKIP:    return mt7902::MCU_CIPHER_TKIP;
    case APPLE80211_CIPHER_AES_OCB:
    case APPLE80211_CIPHER_AES_CCM: return mt7902::MCU_CIPHER_AES_CCMP;
    default:                        return mt7902::MCU_CIPHER_NONE;
    }
}

bool AirportMT7902SkywalkInterface::initWithController(AirportMT7902 *ctrl, mt7902::MT7902Sta *sta) {
    mController = ctrl;
    mSta = sta;
    return init(ctrl);
}

bool AirportMT7902SkywalkInterface::init(IOService *provider) {
    if (!IO80211InfraInterface::init())
        return false;
#ifdef AIRPORT_WCL
    IOEthernetAddress addr = {};
    if (mController && mController->getHardwareAddress(&addr) == kIOReturnSuccess) {
        if (!IO80211SkywalkInterface::init(provider, reinterpret_cast<ether_addr *>(&addr)))
            return false;
    }
#else
    (void)provider;
#endif
    return true;
}

#ifdef AIRPORT_WCL
extern IOCommandGate *gMT7902CommandGate;

IOWorkLoop *AirportMT7902SkywalkInterface::getWorkLoop() const {
    return mController ? mController->getWorkLoop() : nullptr;
}

namespace {
struct WclPostMsgArgs {
    AirportMT7902 *ctrl;
    AirportMT7902SkywalkInterface *iface;
    uint32_t msgId;
    void *data;
    size_t len;
};

IOReturn postWclMessageGated(OSObject *, void *arg0, void *, void *, void *) {
    auto *a = static_cast<WclPostMsgArgs *>(arg0);
    (void)a->ctrl->postMessage(a->iface, a->msgId, a->data, a->len, true);
    return kIOReturnSuccess;
}
} // namespace

void AirportMT7902SkywalkInterface::postWclMessage(uint32_t msgId, void *data, size_t len) {
    if (!mController)
        return;
    auto *wq = mController->getWorkQueue();
    if (wq && !wq->inGate() && gMT7902CommandGate) {
        WclPostMsgArgs args{ mController, this, msgId, data, len };
        (void)gMT7902CommandGate->runAction(postWclMessageGated, &args);
        return;
    }
    (void)mController->postMessage(this, msgId, data, len, true);
}

void AirportMT7902SkywalkInterface::populateWclBeaconBuffer(
    const mt7902::ScannedBss &bss, uint8_t *buf, size_t &outTotalLen) {
    auto *meta = reinterpret_cast<mt7902::wcl::BeaconMetadata *>(buf);
    *meta = {};
    size_t ieLen = bss.ieLen > 0x7ff ? 0x7ff : bss.ieLen;
    meta->channelSpec = static_cast<uint16_t>(
        bss.channel | (bss.band == mt7902::kBand5GHz ? 0xd000U : 0x1000U));
    memcpy(meta->ssid, bss.ssid, bss.ssidLen);
    meta->ssidLength = bss.ssidLen;
    meta->channel = bss.channel;
    memcpy(meta->bssid, bss.bssid, 6);
    meta->rssi = bss.rssi;
    meta->noise = 0;
    meta->snr = 0;
    meta->interval = bss.beaconInterval;
    meta->capability = bss.capability;
    meta->flags = mt7902::wcl::kRssiValid | mt7902::wcl::kSsidPresent;
    uint8_t *ieDst = buf + sizeof(*meta);
    if (ieLen > 0 && mt7902::wcl::validIeChain(bss.ies, ieLen)) {
        memcpy(ieDst, bss.ies, ieLen);
        ieLen = mt7902::wcl::selectWPA2TransitionMode(ieDst, ieLen);
    } else {
        ieLen = 0;
    }
    meta->ieLength = static_cast<uint32_t>(ieLen);
    outTotalLen = sizeof(*meta) + ieLen;
}

void AirportMT7902SkywalkInterface::finishWclJoin(
    uint16_t status, uint16_t reason, IOReturn driverStatus) {
    if (!mWclJoinPending)
        return;
    mWclJoinPending = false;
    mt7902::wcl::ConnectionEvent conn = {};
    conn.status = status;
    conn.reason = reason;
    memcpy(conn.peers[0].bssid, mWclJoinBssid, 6);
    conn.peers[0].driverStatus = driverStatus;
    postWclMessage(mt7902::wcl::kMsgConnectComplete, &conn, sizeof(conn));
    if (status != 0) {
        mWclPortAuthorized = false;
        setLQM(static_cast<unsigned long long>(static_cast<long long>(-2)));
    } else {
        int rssi = mSta ? mSta->link().rssi : -60;
        int quality = rssi >= -70 ? 100 : (rssi >= -85 ? 50 : 20);
        setLQM(static_cast<unsigned long long>(quality));
    }
}
#endif

void AirportMT7902SkywalkInterface::notifyDriverAvailable(bool ready) {
#ifdef AIRPORT_WCL
    if (!ready) {
        if (mWclScanPending) {
            mWclScanPending = false;
            IOReturn st = kIOReturnOffline;
            postWclMessage(mt7902::wcl::kMsgScanDone, &st, sizeof(st));
        }
        finishWclJoin(1, 0, kIOReturnOffline);
        mWclPortAuthorized = false;
        mWclBssValid = false;
        setLQM(static_cast<unsigned long long>(static_cast<long long>(-2)));
    } else {
        uint32_t reset = 1;
        postWclMessage(mt7902::wcl::kMsgResetInterface, &reset, sizeof(reset));
    }

    mt7902::wcl::DriverAvailability avail = {};
    avail.version = 3;
    avail.available = ready ? 1 : 0;
    avail.reason = ready ? 0xe0821803U : 0xe0821804U;
    postWclMessage(mt7902::wcl::kMsgDriverAvailable, &avail, sizeof(avail));
#else
    (void)ready;
#endif
}

void AirportMT7902SkywalkInterface::notifyScanDone() {
#ifdef AIRPORT_WCL
    if (mWclScanPending) {
        if (mSta) {
            uint64_t nowMs = mSta->device().bus().nowMs();
            mSta->scanCache().resetCursor();
            uint8_t scanBuf[sizeof(mt7902::wcl::BeaconMetadata) + 0x800] = {};
            while (const auto *bss = mSta->scanCache().nextResult()) {
                if (nowMs >= bss->lastSeenMs && nowMs - bss->lastSeenMs <= 60000) {
                    size_t totalLen = 0;
                    memset(scanBuf, 0, sizeof(scanBuf));
                    populateWclBeaconBuffer(*bss, scanBuf, totalLen);
                    postWclMessage(mt7902::wcl::kMsgScanResult, scanBuf, totalLen);
                }
            }
            mSta->scanCache().resetCursor();
        }
        mWclScanPending = false;
        IOReturn status = kIOReturnSuccess;
        postWclMessage(mt7902::wcl::kMsgScanDone, &status, sizeof(status));
    }
#else
    UInt32 msg = 0;
    postMessage(APPLE80211_M_SCAN_DONE, &msg, sizeof(msg), false);
#endif
}

void AirportMT7902SkywalkInterface::notifyBssDiscovered(const mt7902::ScannedBss &bss) {
#ifdef AIRPORT_WCL
    uint8_t scanBuf[sizeof(mt7902::wcl::BeaconMetadata) + 0x800] = {};
    size_t totalLen = 0;
    populateWclBeaconBuffer(bss, scanBuf, totalLen);
    postWclMessage(mt7902::wcl::kMsgScanResult, scanBuf, totalLen);
#else
    (void)bss;
#endif
}

void AirportMT7902SkywalkInterface::notifyAssocDone(bool success, uint16_t statusCode) {
#ifdef AIRPORT_WCL
    const auto &lk = mSta->link();
    mt7902::wcl::AssociationEvent ev = {};
    ev.status = success ? 0 : (statusCode ? statusCode : 1);
    ev.reason = 0;
    ev.mlo = 1; // BSSID included flag (verified in T2UB AssociationNotifications.cpp)
    memcpy(ev.bssid, lk.bssid, 6);
    ev.driverStatus = success ? kIOReturnSuccess : kIOReturnError;
    mWclAssocNotified = success;
    postWclMessage(mt7902::wcl::kMsgAssocComplete, &ev, sizeof(ev));

    if (!success) {
        finishWclJoin(ev.status, 0, kIOReturnError);
        return;
    }

    // Seed BSS info immediately from scan cache if we already captured a beacon from this AP
    if (const auto *cached = mSta->scanCache().findByBssid(lk.bssid)) {
        notifyBeacon(*cached);
    }
#else
    (void)success;
    (void)statusCode;
    postMessage(APPLE80211_M_ASSOC_DONE, nullptr, 0, false);
#endif
}

void AirportMT7902SkywalkInterface::notifyLinkChanged(bool up) {
#ifdef AIRPORT_WCL
    mWclPortAuthorized = up;
    if (!up) {
        if (mWclLinkIndicated) {
            mt7902::wcl::LinkStatusEvent ev = {};
            memcpy(ev.bssid, mWclJoinBssid, 6);
            ev.up = 0;
            ev.reason = 255;
            mWclLinkIndicated = false;
            mWclBssValid = false;
            postWclMessage(mt7902::wcl::kMsgLinkStatus, &ev, sizeof(ev));
        }
        setLQM(static_cast<unsigned long long>(static_cast<long long>(-2)));
        return;
    }

    if (mWclJoinPending && mWclFirstBeaconSent)
        finishWclJoin(0, 0, kIOReturnSuccess);

    int rssi = mSta ? mSta->link().rssi : -60;
    int quality = rssi >= -70 ? 100 : (rssi >= -85 ? 50 : 20);
    setLQM(static_cast<unsigned long long>(quality));
#else
    (void)up;
    postMessage(APPLE80211_M_LINK_CHANGED, nullptr, 0, false);
    postMessage(APPLE80211_M_BSSID_CHANGED, nullptr, 0, false);
    postMessage(APPLE80211_M_SSID_CHANGED, nullptr, 0, false);
#endif
}

void AirportMT7902SkywalkInterface::notifyDeauth(uint16_t reasonCode, bool isDisassoc) {
#ifdef AIRPORT_WCL
    mWclPortAuthorized = false;
    setLQM(static_cast<unsigned long long>(static_cast<long long>(-2)));
    mt7902::wcl::DeauthEvent ev = {};
    ev.reason = reasonCode;
    memcpy(ev.bssid, mWclJoinBssid, 6);
    ev.disassociation = isDisassoc ? 1 : 0;
    postWclMessage(mt7902::wcl::kMsgDeauth, &ev, sizeof(ev));
    finishWclJoin(1, reasonCode, kIOReturnOffline);
#else
    (void)reasonCode;
    (void)isDisassoc;
    postMessage(APPLE80211_M_DEAUTH_RECEIVED, nullptr, 0, false);
#endif
}

void AirportMT7902SkywalkInterface::notifyBeacon(const mt7902::ScannedBss &bss) {
#ifdef AIRPORT_WCL
    if (memcmp(bss.bssid, mWclJoinBssid, 6) != 0)
        return;

    size_t totalLen = 0;
    memset(mWclBeaconMsg, 0, sizeof(mWclBeaconMsg));
    populateWclBeaconBuffer(bss, mWclBeaconMsg, totalLen);
    mWclBssValid = (totalLen <= sizeof(mWclBeaconMsg));

    if (bss.rawFrameLen >= 36 && bss.rawFrameLen <= 0x800) {
        uint32_t flen = bss.rawFrameLen;
        memset(mWclBeaconInfo, 0, sizeof(mWclBeaconInfo));
        memcpy(mWclBeaconInfo, &flen, sizeof(flen));
        memcpy(mWclBeaconInfo + 4, bss.rawFrame, flen);
    } else {
        // Synthesize minimal raw 802.11 beacon header + IEs if rawFrame was truncated
        uint32_t ieCopy = bss.ieLen > 0x700 ? 0x700 : bss.ieLen;
        uint32_t flen = 36 + ieCopy;
        memset(mWclBeaconInfo, 0, sizeof(mWclBeaconInfo));
        memcpy(mWclBeaconInfo, &flen, sizeof(flen));
        uint8_t *f = mWclBeaconInfo + 4;
        f[0] = 0x80; // Beacon FC
        memset(f + 4, 0xff, 6);
        memcpy(f + 10, bss.bssid, 6);
        memcpy(f + 16, bss.bssid, 6);
        uint64_t tsfUs = mSta ? mSta->device().bus().nowMs() * 1000ULL : 1000ULL;
        memcpy(f + 24, &tsfUs, 8);
        f[32] = static_cast<uint8_t>(bss.beaconInterval & 0xffU);
        f[33] = static_cast<uint8_t>((bss.beaconInterval >> 8) & 0xffU);
        f[34] = static_cast<uint8_t>(bss.capability & 0xffU);
        f[35] = static_cast<uint8_t>((bss.capability >> 8) & 0xffU);
        if (ieCopy > 0)
            memcpy(f + 36, bss.ies, ieCopy);
    }
    ++mWclAssociatedBeacons;

    if (mWclJoinPending || mWclPortAuthorized)
        postWclMessage(mt7902::wcl::kMsgScanResult, mWclBeaconMsg, totalLen);

    if (mWclJoinPending && mWclAssocNotified && !mWclFirstBeaconSent && mWclBssValid) {
        mWclFirstBeaconSent = true;
        mt7902::wcl::LinkStatusEvent link = {};
        memcpy(link.bssid, mWclJoinBssid, 6);
        link.up = 1;
        link.reason = 255;
        mWclLinkIndicated = true;
        postWclMessage(mt7902::wcl::kMsgLinkStatus, &link, sizeof(link));

        mt7902::wcl::FirstBeaconEvent fb = {};
        memcpy(fb.peer.bssid, mWclJoinBssid, 6);
        postWclMessage(mt7902::wcl::kMsgFirstBeacon, &fb, sizeof(fb));

        if (mWclPortAuthorized)
            finishWclJoin(0, 0, kIOReturnSuccess);
    }

    if (mWclPortAuthorized && (mWclAssociatedBeacons % 8 == 0)) {
#if __IO80211_TARGET == __MAC_15_2
        mt7902::wcl::SequoiaLqmUpdate lqm = {};
        lqm.rssiValid = 1;
        lqm.rssi = bss.rssi;
        postWclMessage(mt7902::wcl::kMsgLqmUpdate, &lqm, sizeof(lqm));
#else
        mt7902::wcl::LqmBeaconUpdate lqm = {};
        lqm.receivedDelta = 8;
        lqm.scheduledDelta = 8;
        lqm.beaconValid = 1;
        lqm.snapshotValid = 1;
        lqm.countersValid = 1;
        postWclMessage(mt7902::wcl::kMsgLqmUpdate, &lqm, sizeof(lqm));
#endif
        int quality = bss.rssi >= -70 ? 100 : (bss.rssi >= -85 ? 50 : 20);
        setLQM(static_cast<unsigned long long>(quality));
    }
#else
    (void)bss;
#endif
}

// --- Standard Skywalk IO80211InfraProtocol methods ---

IOReturn AirportMT7902SkywalkInterface::getSSID(apple80211_ssid_data *sd) {
    if (!mSta || mSta->link().state < mt7902::kStaAssociated)
        return 6;
    memset(sd, 0, sizeof(*sd));
    sd->version = APPLE80211_VERSION;
    sd->ssid_len = mSta->link().ssidLen;
    memcpy(sd->ssid_bytes, mSta->link().ssid, mSta->link().ssidLen);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getAUTH_TYPE(apple80211_authtype_data *ad) {
    if (!mSta) return kIOReturnNotReady;
    ad->version = APPLE80211_VERSION;
    ad->authtype_lower = mSta->link().authLower;
    ad->authtype_upper = mSta->link().authUpper;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getCHANNEL(apple80211_channel_data *cd) {
    if (!mSta || mSta->link().state < mt7902::kStaAssociated)
        return 6;
    const auto &lk = mSta->link();
    memset(cd, 0, sizeof(*cd));
    cd->version = APPLE80211_VERSION;
    cd->channel.version = APPLE80211_VERSION;
    cd->channel.channel = lk.channel;
    if (const auto *entry = mt7902::findChannel(lk.channel, lk.band)) {
        cd->channel.flags = mt7902::channelToAppleFlags(*entry, lk.bw);
    } else {
        uint32_t bwFlag = mt7902::kAppleChanFlag20Mhz;
        if (lk.bw == mt7902::CMD_CBW_80MHZ)
            bwFlag = mt7902::kAppleChanFlag80Mhz;
        else if (lk.bw == mt7902::CMD_CBW_40MHZ)
            bwFlag = mt7902::kAppleChanFlag40Mhz;
        cd->channel.flags =
            (lk.band == mt7902::kBand5GHz ? mt7902::kAppleChanFlag5Ghz : mt7902::kAppleChanFlag2Ghz) |
            bwFlag | mt7902::kAppleChanFlagActive;
    }
    if (lk.bw == mt7902::CMD_CBW_40MHZ && lk.centerChannel > lk.channel)
        cd->channel.flags |= mt7902::kAppleChanFlagExtAbv;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getPOWERSAVE(apple80211_powersave_data *ps) {
    ps->version = APPLE80211_VERSION;
    ps->powersave_level = APPLE80211_POWERSAVE_MODE_DISABLED;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getTXPOWER(apple80211_txpower_data *tp) {
    tp->version = APPLE80211_VERSION;
    tp->txpower_unit = APPLE80211_UNIT_PERCENT;
    tp->txpower = 100;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getRATE(apple80211_rate_data *rt) {
    if (!mSta) return kIOReturnNotReady;
    memset(rt, 0, sizeof(*rt));
    rt->version = APPLE80211_VERSION;
    rt->num_radios = 1;
    rt->rate[0] = mSta->link().txRateMbps;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getBSSID(apple80211_bssid_data *bd) {
    if (!mSta || mSta->link().state < mt7902::kStaAssociated)
        return 6;
    memset(bd, 0, sizeof(*bd));
    bd->version = APPLE80211_VERSION;
    memcpy(bd->bssid.octet, mSta->link().bssid, 6);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getSCAN_RESULT(apple80211_scan_result *sr) {
    if (!mSta || !sr) return kIOReturnBadArgument;
    const mt7902::ScannedBss *bss = mSta->scanCache().nextResult();
    if (!bss)
        return static_cast<IOReturn>(0xe0820446);

    memset(sr, 0, sizeof(*sr));
    sr->version = APPLE80211_VERSION;
    sr->asr_channel.version = APPLE80211_VERSION;
    sr->asr_channel.channel = bss->channel;
    sr->asr_channel.flags =
        (bss->band == mt7902::kBand5GHz ? mt7902::kAppleChanFlag5Ghz : mt7902::kAppleChanFlag2Ghz) |
        mt7902::kAppleChanFlag20Mhz | mt7902::kAppleChanFlagActive;
    sr->asr_noise = static_cast<int8_t>(bss->noise);
    sr->asr_rssi = static_cast<int8_t>(bss->rssi);
    sr->asr_beacon_int = bss->beaconInterval;
    sr->asr_cap = bss->capability;
    memcpy(sr->asr_bssid, bss->bssid, 6);
    sr->asr_ssid_len = bss->ssidLen;
    memcpy(sr->asr_ssid, bss->ssid, bss->ssidLen);
    if (bss->ieLen > 0) {
        uint32_t ieCopy = bss->ieLen > sizeof(sr->asr_ie_data)
                              ? static_cast<uint32_t>(sizeof(sr->asr_ie_data))
                              : bss->ieLen;
        sr->asr_ie_len = ieCopy;
        memcpy(sr->asr_ie_data, bss->ies, ieCopy);
    }
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getCURRENT_NETWORK(apple80211_scan_result *sr) {
    if (!mSta || mSta->link().state < mt7902::kStaAssociated || !sr)
        return kIOReturnNotFound;
    const auto &lk = mSta->link();
    memset(sr, 0, sizeof(*sr));
    sr->version = APPLE80211_VERSION;
    sr->asr_channel.version = APPLE80211_VERSION;
    sr->asr_channel.channel = lk.channel;
    sr->asr_rssi = static_cast<int8_t>(lk.rssi);
    sr->asr_noise = static_cast<int8_t>(lk.noise);
    sr->asr_beacon_int = lk.beaconInterval;
    sr->asr_cap = lk.capability;
    memcpy(sr->asr_bssid, lk.bssid, 6);
    sr->asr_ssid_len = lk.ssidLen;
    memcpy(sr->asr_ssid, lk.ssid, lk.ssidLen);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getSTATE(apple80211_state_data *st) {
    if (!mSta) return kIOReturnNotReady;
    memset(st, 0, sizeof(*st));
    st->version = APPLE80211_VERSION;
    switch (mSta->link().state) {
    case mt7902::kStaScanning:       st->state = APPLE80211_S_SCAN; break;
    case mt7902::kStaAuthenticating: st->state = APPLE80211_S_AUTH; break;
    case mt7902::kStaAssociating:    st->state = APPLE80211_S_ASSOC; break;
    case mt7902::kStaAssociated:     st->state = APPLE80211_S_ASSOC; break;
    case mt7902::kStaRun:            st->state = APPLE80211_S_RUN; break;
    default:                         st->state = APPLE80211_S_INIT; break;
    }
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getPHY_MODE(apple80211_phymode_data *pm) {
    pm->version = APPLE80211_VERSION;
    pm->phy_mode = APPLE80211_MODE_11A | APPLE80211_MODE_11B | APPLE80211_MODE_11G |
                   APPLE80211_MODE_11N | APPLE80211_MODE_11AC | APPLE80211_MODE_11AX;
    pm->active_phy_mode = APPLE80211_MODE_11AX;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getOP_MODE(apple80211_opmode_data *om) {
    om->version = APPLE80211_VERSION;
    om->op_mode = APPLE80211_M_STA;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getRSSI(apple80211_rssi_data *rd) {
    if (!mSta) return kIOReturnNotReady;
    memset(rd, 0, sizeof(*rd));
    rd->version = APPLE80211_VERSION;
    rd->num_radios = 1;
    rd->rssi_unit = APPLE80211_UNIT_DBM;
    rd->rssi[0] = rd->aggregate_rssi = mSta->link().rssi;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getNOISE(apple80211_noise_data *nd) {
    if (!mSta) return kIOReturnNotReady;
    memset(nd, 0, sizeof(*nd));
    nd->version = APPLE80211_VERSION;
    nd->num_radios = 1;
    nd->noise_unit = APPLE80211_UNIT_DBM;
    nd->noise[0] = nd->aggregate_noise = mSta->link().noise;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getSUPPORTED_CHANNELS(apple80211_sup_channel_data *ch) {
    if (!mSta) return kIOReturnNotReady;
    memset(ch, 0, sizeof(*ch));
    ch->version = APPLE80211_VERSION;
    const auto &cap = mSta->device().capability();
    mt7902::ChannelInfoEntry list[APPLE80211_MAX_CHANNELS];
    // Expose 2.4/5 GHz until WPA3-SAE + PMF (required by 6 GHz) is implemented
    size_t n = mt7902::getSupportedChannels(cap.has2Ghz, cap.has5Ghz, false,
                                            list, APPLE80211_MAX_CHANNELS);
    ch->num_channels = static_cast<uint32_t>(n);
    for (size_t i = 0; i < n; ++i) {
        ch->supported_channels[i].version = APPLE80211_VERSION;
        ch->supported_channels[i].channel = list[i].channel;
        // Every channel must carry 20 MHz base flag so macOS WCL scans all 2.4 & 5 GHz channels
        // (40/80 MHz capability is advertised separately in getCHANNELS_INFO NativeChannelInfo).
        ch->supported_channels[i].flags = mt7902::channelToAppleFlags(
            list[i], mt7902::CMD_CBW_20MHZ);
    }
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getLOCALE(apple80211_locale_data *ld) {
    ld->version = APPLE80211_VERSION;
    ld->locale = APPLE80211_LOCALE_FCC;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getDEAUTH(apple80211_deauth_data *dd) {
    if (!mSta) return kIOReturnNotReady;
    memset(dd, 0, sizeof(*dd));
    dd->version = APPLE80211_VERSION;
    dd->deauth_reason = mSta->link().lastDeauthReason;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getRATE_SET(apple80211_rate_set_data *rs) {
    memset(rs, 0, sizeof(*rs));
    rs->version = APPLE80211_VERSION;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getRSN_IE(apple80211_rsn_ie_data *rsn) {
    if (!mSta) return kIOReturnNotReady;
    if (mSta->link().rsnIeLen == 0)
        return kIOReturnNotReady;
    memset(rsn, 0, sizeof(*rsn));
    rsn->version = APPLE80211_VERSION;
    rsn->len = mSta->link().rsnIeLen;
    memcpy(rsn->ie, mSta->link().rsnIe, rsn->len);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getAP_IE_LIST(apple80211_ap_ie_data *ap) {
    if (!mSta || !mSta->link().apIeLen) return kIOReturnNotFound;
    uint32_t copyLen = mSta->link().apIeLen < ap->len ? mSta->link().apIeLen : ap->len;
    ap->version = APPLE80211_VERSION;
    ap->len = copyLen;
    if (ap->ie_data && copyLen > 0)
        memcpy(ap->ie_data, mSta->link().apIes, copyLen);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getASSOCIATION_STATUS(apple80211_assoc_status_data *as) {
    if (!mSta) return kIOReturnNotReady;
    as->version = APPLE80211_VERSION;
    as->status = mSta->link().lastAssocStatus;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getMCS(apple80211_mcs_data *md) {
    if (!mSta) return kIOReturnNotReady;
    md->version = APPLE80211_VERSION;
    md->index = mSta->link().mcs;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getMCS_INDEX_SET(apple80211_mcs_index_set_data *ms) {
    memset(ms, 0, sizeof(*ms));
    ms->version = APPLE80211_VERSION;
    ms->mcs_set_map[0] = 0xff;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getVHT_MCS_INDEX_SET(apple80211_vht_mcs_index_set_data *vs) {
    memset(vs, 0, sizeof(*vs));
    vs->version = APPLE80211_VERSION;
    vs->mcs_map = 0xfffe;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getMCS_VHT(apple80211_mcs_vht_data *mv) {
    if (!mSta) return kIOReturnNotReady;
    const auto &lk = mSta->link();
    memset(mv, 0, sizeof(*mv));
    mv->version = APPLE80211_VERSION;
    mv->index = lk.mcs;
    mv->nss = lk.nss;
    mv->bw = (lk.bw == mt7902::CMD_CBW_80MHZ) ? 80 : (lk.bw == mt7902::CMD_CBW_40MHZ ? 40 : 20);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getNSS(apple80211_nss_data *nd) {
    if (!mSta) return kIOReturnNotReady;
    nd->version = APPLE80211_VERSION;
    nd->nss = mSta->device().capability().nss;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getHW_ADDR(apple80211_hw_mac_address *hw) {
    if (!mSta || !hw) return kIOReturnBadArgument;
    mt7902::wcl::HardwareAddress wire = {};
    wire.version = 1;
    memcpy(wire.address, mSta->device().capability().macAddr, 6);
    memcpy(hw, &wire, sizeof(wire));
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getLINK_CHANGED_EVENT_DATA(apple80211_link_changed_event_data *lc) {
    if (!mSta) return kIOReturnNotReady;
    memset(lc, 0, sizeof(*lc));
    lc->isLinkDown = (mSta->link().state < mt7902::kStaRun);
    lc->rssi = mSta->link().rssi;
    lc->voluntary = true;
    lc->reason = mSta->link().lastDeauthReason;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getWCL_VALID_CHANNEL_COUNT(unsigned long *count) {
    if (!count || !mSta) return kIOReturnBadArgument;
    const auto &cap = mSta->device().capability();
    mt7902::ChannelInfoEntry list[96];
    *count = mt7902::getSupportedChannels(cap.has2Ghz, cap.has5Ghz, false, list, 96);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setAUTH_TYPE(apple80211_authtype_data *ad) {
    (void)ad;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setCIPHER_KEY(apple80211_key *key) {
    if (!mSta || !key) return kIOReturnBadArgument;
#ifdef AIRPORT_WCL
    // On WCL, WPA2-Personal PMK is supplied in setWCL_ASSOCIATE; accept PMK/MSK gracefully
    if (key->key_cipher_type == APPLE80211_CIPHER_PMK ||
        key->key_cipher_type == APPLE80211_CIPHER_PMKSA ||
        key->key_cipher_type == APPLE80211_CIPHER_MSK)
        return kIOReturnSuccess;
#endif
    bool pairwise = (key->key_flags & APPLE80211_KEY_FLAG_UNICAST) != 0;
    uint8_t cipher = mapSkywalkCipher(key->key_cipher_type);
    bool set = (key->key_len > 0 && cipher != mt7902::MCU_CIPHER_NONE);
    return (mSta->installCipherKey(cipher, static_cast<uint8_t>(key->key_index & 0x3U),
                                   key->key, key->key_len, pairwise, set) == mt7902::kOk)
               ? kIOReturnSuccess
               : kIOReturnError;
}

IOReturn AirportMT7902SkywalkInterface::setSCAN_REQ(apple80211_scan_data *req) {
    if (!mSta || !req) return kIOReturnBadArgument;
    mt7902::ScanRequestParams p;
    p.active = (req->scan_type != APPLE80211_SCAN_TYPE_PASSIVE);
    if (req->ssid_len > 0 && req->ssid_len <= 32) {
        p.ssidCount = 1;
        p.ssidLens[0] = static_cast<uint8_t>(req->ssid_len);
        memcpy(p.ssids[0], req->ssid, req->ssid_len);
    }
    return (mSta->startScan(p) == mt7902::kOk) ? kIOReturnSuccess : kIOReturnBusy;
}

IOReturn AirportMT7902SkywalkInterface::setASSOCIATE(apple80211_assoc_data *req) {
    if (!mSta || !req) return kIOReturnBadArgument;
    mt7902::AssocRequestParams p;
    p.ssidLen = req->ad_ssid_len > 32 ? 32 : static_cast<uint8_t>(req->ad_ssid_len);
    memcpy(p.ssid, req->ad_ssid, p.ssidLen);
    bool hasBssid = false;
    for (int i = 0; i < 6; ++i) hasBssid |= (req->ad_bssid.octet[i] != 0);
    if (hasBssid) {
        memcpy(p.bssid, req->ad_bssid.octet, 6);
        p.hasBssid = true;
    }
    p.authLower = req->ad_auth_lower;
    p.authUpper = req->ad_auth_upper;
    if (req->ad_rsn_ie_len > 0 && req->ad_rsn_ie_len <= sizeof(p.rsnIe)) {
        memcpy(p.rsnIe, req->ad_rsn_ie, req->ad_rsn_ie_len);
        p.rsnIeLen = static_cast<uint16_t>(req->ad_rsn_ie_len);
    }
    return (mSta->associate(p) == mt7902::kOk) ? kIOReturnSuccess : kIOReturnError;
}

IOReturn AirportMT7902SkywalkInterface::setDISASSOCIATE(apple80211_disassoc_data *) {
    if (mSta) (void)mSta->disassociate(8);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setDEAUTH(apple80211_deauth_data *) {
    if (mSta) (void)mSta->disassociate(3);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setRSN_IE(apple80211_rsn_ie_data *) {
#ifdef AIRPORT_WCL
    return kIOReturnUnsupported;
#else
    return kIOReturnSuccess;
#endif
}

IOReturn AirportMT7902SkywalkInterface::setSCANCACHE_CLEAR(void *) {
    if (mSta) mSta->scanCache().clear();
    return kIOReturnSuccess;
}

#ifdef AIRPORT_WCL
// --- macOS 15.2 Sequoia & macOS 26 Tahoe Native WCL methods ---

IOReturn AirportMT7902SkywalkInterface::getCHANNELS_INFO(apple80211_channels_info *out) {
    if (!mSta || !out) return kIOReturnBadArgument;
    auto *native = reinterpret_cast<mt7902::wcl::NativeChannelInfo *>(out);
    memset(native, 0, sizeof(*native));
    memcpy(native->country, mSta->countryCode(), 2);

    const auto &cap = mSta->device().capability();
    mt7902::ChannelInfoEntry list[96];
    size_t n = mt7902::getSupportedChannels(cap.has2Ghz, cap.has5Ghz, false, list, 96);
    for (size_t i = 0; i < n; ++i) {
        mt7902::wcl::appendNativeChannel(
            *native, list[i].channel, list[i].band == mt7902::kBand5GHz,
            list[i].dfs, list[i].passive, list[i].allow40, list[i].allow80);
    }
    return native->count ? kIOReturnSuccess : kIOReturnNotReady;
}

IOReturn AirportMT7902SkywalkInterface::getWCL_CHANNELS_INFO(apple80211ChannelInfo *out) {
    if (!mSta || !out) return kIOReturnBadArgument;
    auto *info = reinterpret_cast<mt7902::wcl::ChannelInfo *>(out);
    memset(info, 0, sizeof(*info));
    memcpy(info->country, mSta->countryCode(), 2);
    info->supports6GHz = 0;

    mt7902::ChannelInfoEntry list[96];
    size_t n = mt7902::getSupportedChannels(
        mSta->device().capability().has2Ghz,
        mSta->device().capability().has5Ghz,
        false, list, 96);
    info->count = static_cast<uint16_t>(n);
    for (size_t i = 0; i < n; ++i) {
        info->channels[i].channel = list[i].channel;
        info->channels[i].channelSpec = static_cast<uint16_t>(
            list[i].channel | (list[i].band == mt7902::kBand5GHz ? 0xd000U : 0x1000U));
        if (list[i].dfs)
            info->channels[i].flags |= 2;
        if (list[i].passive)
            info->channels[i].flags |= 4;
    }
    return info->count ? kIOReturnSuccess : kIOReturnNotReady;
}

IOReturn AirportMT7902SkywalkInterface::getWCL_BSS_INFO(apple80211_beacon_msg *out) {
    if (!out) return kIOReturnBadArgument;
    if (!mWclBssValid)
        return kIOReturnNotReady;
    memcpy(out, mWclBeaconMsg, sizeof(mWclBeaconMsg));
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *out) {
    if (!out) return kIOReturnBadArgument;
    if (!mSta || mSta->link().state < mt7902::kStaAssociated)
        return kIOReturnNotReady;

    auto *bytes = reinterpret_cast<uint8_t *>(out);
#if __IO80211_TARGET == __MAC_15_2
    memset(bytes, 0, 0x1e0);
#else
    memset(bytes, 0, 0x214);
#endif
    (void)getRATE_SET(reinterpret_cast<apple80211_rate_set_data *>(bytes));
    (void)getMCS_INDEX_SET(reinterpret_cast<apple80211_mcs_index_set_data *>(bytes + 0xbc));

    apple80211_vht_mcs_index_set_data vht = {};
    vht.version = APPLE80211_VERSION;
    vht.mcs_map = 0xfffe;
    memcpy(bytes + 0xcc, &vht, sizeof(vht));

    uint32_t heVersion = APPLE80211_VERSION;
    uint16_t heMap = 0xfffe;
    memcpy(bytes + 0xd4, &heVersion, sizeof(heVersion));
    memcpy(bytes + 0xd8, &heMap, sizeof(heMap));

    const auto &lk = mSta->link();
    if (lk.rsnIeLen > 0 && lk.rsnIeLen <= 64) {
#if __IO80211_TARGET == __MAC_15_2
        memcpy(bytes + 0xdd, lk.rsnIe, lk.rsnIeLen);
#else
        memcpy(bytes + 0x113, lk.rsnIe, lk.rsnIeLen);
#endif
    }
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *out) {
    if (!mSta || !out) return kIOReturnBadArgument;
    auto *tc = reinterpret_cast<mt7902::wcl::TrafficCounters *>(out);
    memset(tc, 0, sizeof(*tc));
    const auto &st = mSta->device().mac().stats();
    tc->infraTx = st.txPackets;
    tc->infraRx = st.rxPackets;
    tc->timestampNanoseconds = mSta->device().bus().nowMs() * 1000000ULL;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *out) {
    if (!out) return kIOReturnBadArgument;
    memset(out, 0, 4);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getBEACON_INFO(apple80211_beacon_info_t *out) {
    if (!out) return kIOReturnBadArgument;
    if (!mWclBssValid || mWclAssociatedBeacons == 0)
        return kIOReturnNotReady;
    memcpy(out, mWclBeaconInfo, sizeof(mWclBeaconInfo));
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setWCL_SCAN_REQ(apple80211ScanRequest *rawReq) {
    if (!mSta || !rawReq) return kIOReturnBadArgument;
    if (!mSta->isPoweredOn())
        return kIOReturnNotReady;
    if (mWclScanPending || mWclJoinPending || mSta->isScanning())
        return kIOReturnBusy;

    mt7902::wcl::DecodedScanRequest decoded = {};
    if (!mt7902::wcl::decodeScan(rawReq, 0x1550, decoded)) {
        MTK_LOG("WCL scan decode failed");
        return kIOReturnBadArgument;
    }

    mt7902::ScanRequestParams params = {};
    params.active = (decoded.type != 2);
    if (decoded.type != 2 && decoded.ssidLength > 0 && decoded.ssidLength <= 32) {
        params.ssidCount = 1;
        params.ssidLens[0] = static_cast<uint8_t>(decoded.ssidLength);
        memcpy(params.ssids[0], decoded.ssid, decoded.ssidLength);
    }
    if (decoded.channelCount > 0) {
        const auto &cap = mSta->device().capability();
        mt7902::ChannelInfoEntry sup[96];
        size_t n = mt7902::getSupportedChannels(cap.has2Ghz, cap.has5Ghz, false, sup, 96);
        for (size_t i = 0; i < n && params.channelCount < 64; ++i) {
            uint8_t ch = sup[i].channel;
            if (decoded.channels[ch / 8] & (1U << (ch % 8))) {
                auto &dst = params.channels[params.channelCount++];
                dst.channel_num = ch;
                dst.band = static_cast<uint8_t>(sup[i].band) + 1U;
            }
        }
        if (params.channelCount == 0) {
            const uint8_t *rawBytes = reinterpret_cast<const uint8_t *>(rawReq);
            uint32_t firstCh = decoded.channelCount ? mt7902::wcl::rd32(rawBytes + 0x5c) : 0;
            uint32_t firstFlags = decoded.channelCount ? mt7902::wcl::rd32(rawBytes + 0x60) : 0;
            MTK_LOG("WCL scan filtered to 0 ch (rawCnt=%u type=%u firstCh=%u firstFlags=0x%x)",
                    decoded.channelCount, decoded.type, firstCh, firstFlags);
            return kIOReturnUnsupported;
        }
    }

    uint8_t firstCh = params.channelCount ? params.channels[0].channel_num : 0;
    uint8_t lastCh = params.channelCount ? params.channels[params.channelCount - 1].channel_num : 0;
    MTK_LOG("WCL scan request: id=%u type=%u ssidLen=%u chCnt=%u (%u..%u, rawCnt=%u)",
            decoded.identifier, decoded.type, decoded.ssidLength, params.channelCount,
            firstCh, lastCh, decoded.channelCount);
    mWclScanPending = true;
    if (mSta->startScan(params) != mt7902::kOk) {
        mWclScanPending = false;
        return kIOReturnBusy;
    }
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setWCL_SCAN_ABORT(void *) {
    if (mSta) (void)mSta->abortScan();
    if (mWclScanPending) {
        mWclScanPending = false;
        IOReturn st = kIOReturnAborted;
        postWclMessage(mt7902::wcl::kMsgScanDone, &st, sizeof(st));
    }
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setWCL_ASSOCIATE(apple80211AssocCandidates *rawCand) {
    if (!mSta || !rawCand) return kIOReturnBadArgument;
    if (!mSta->isPoweredOn())
        return kIOReturnNotReady;

    mt7902::wcl::DecodedJoinRequest join = {};
    if (!mt7902::wcl::decodeJoin(rawCand, 0x2cc, join))
        return kIOReturnBadArgument;

    if (mWclScanPending || mSta->isScanning()) {
        (void)mSta->abortScan();
        if (mWclScanPending) {
            mWclScanPending = false;
            IOReturn st = kIOReturnAborted;
            postWclMessage(mt7902::wcl::kMsgScanDone, &st, sizeof(st));
        }
    }

    mt7902::AssocRequestParams req = {};
    req.ssidLen = static_cast<uint8_t>(join.ssidLength);
    memcpy(req.ssid, join.ssid, req.ssidLen);
    memcpy(req.bssid, join.bssid, 6);
    req.hasBssid = true;
    req.authLower = join.lowerAuth;
    req.authUpper = join.upperAuth;
    req.channelHint = static_cast<uint8_t>(join.channelSpec & 0xffU);
    if (join.rsnLength > 0 && join.rsnLength <= sizeof(req.rsnIe)) {
        memcpy(req.rsnIe, join.rsn, join.rsnLength);
        req.rsnIeLen = join.rsnLength;
    }
    if (join.upperAuth != 0 && join.keyLength == 32) {
        memcpy(req.pmk, join.key, 32);
        req.hasPmk = true;
    }

    MTK_LOG("WCL join request: BSSID %02x:%02x:%02x:%02x:%02x:%02x ch=%u upperAuth=0x%x keyLen=%u",
            req.bssid[0], req.bssid[1], req.bssid[2],
            req.bssid[3], req.bssid[4], req.bssid[5],
            req.channelHint, req.authUpper, join.keyLength);

    memcpy(mWclJoinBssid, req.bssid, 6);
    mWclJoinPending = true;
    mWclAssocNotified = false;
    mWclFirstBeaconSent = false;
    mWclPortAuthorized = false;
    mWclBssValid = false;
    mWclLinkIndicated = false;
    mWclAssociatedBeacons = 0;
    memset(mWclBeaconInfo, 0, sizeof(mWclBeaconInfo));

    int err = mSta->associate(req);
    memset(&join, 0, sizeof(join));
    memset(req.pmk, 0, sizeof(req.pmk));
    if (err != mt7902::kOk) {
        mWclJoinPending = false;
        return kIOReturnError;
    }
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setWCL_LEAVE_NETWORK(apple80211_leave_network *) {
    mWclJoinPending = false;
    mWclPortAuthorized = false;
    mWclBssValid = false;
    mWclAssociatedBeacons = 0;
    setLQM(static_cast<unsigned long long>(static_cast<long long>(-2)));
    if (mSta) (void)mSta->disassociate(8);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *data) {
    if (!data)
        return kIOReturnBadArgument;
#if __IO80211_TARGET == __MAC_15_2
    const auto *bytes = reinterpret_cast<const uint8_t *>(data);
    if (bytes[6]) {
        if (bytes[8])
            setCurrentApAddress(reinterpret_cast<ether_addr *>(data));
        if (bytes[7])
            setLinkState(kIO80211NetworkLinkUp, 0, false, 0);
    } else {
        uint32_t debounce = mt7902::wcl::rd32(bytes + 12);
        setCurrentApAddress(nullptr);
        setLinkState(kIO80211NetworkLinkDown, 0, debounce != 0, debounce);
    }
    return kIOReturnSuccess;
#else
    return IO80211InfraInterface::setWCL_LINK_STATE_UPDATE(data);
#endif
}

#if __IO80211_TARGET == __MAC_15_2
IOReturn AirportMT7902SkywalkInterface::setWCL_JOIN_ABORT(void *) {
    (void)setWCL_LEAVE_NETWORK(nullptr);
    postWclMessage(mt7902::wcl::kMsgJoinAbortComplete, nullptr, 0);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setMacAddress(ether_addr &addr) {
    return mController ? mController->setHardwareAddress(&addr, 6) : kIOReturnNotReady;
}
#else
IOReturn AirportMT7902SkywalkInterface::setWCL_JOIN_ABORT(apple80211_wcl_abort_join *) {
    (void)setWCL_LEAVE_NETWORK(nullptr);
    postWclMessage(mt7902::wcl::kMsgJoinAbortComplete, nullptr, 0);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setMacAddress(mloAddrArray &addrs) {
    return mController ? mController->setHardwareAddress(&addrs, 6) : kIOReturnNotReady;
}
#endif

#endif // AIRPORT_WCL

#endif // KERNEL && IO80211FAMILY_V2
