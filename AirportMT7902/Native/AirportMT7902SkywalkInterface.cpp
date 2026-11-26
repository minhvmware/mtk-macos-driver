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
    mt7902::wcl::DriverAvailability avail = {};
    avail.version = 1;
    avail.available = 1;
    postMessage(mt7902::wcl::kMsgDriverAvailable, &avail, sizeof(avail), false);
#else
    (void)provider;
#endif
    return true;
}

void AirportMT7902SkywalkInterface::notifyScanDone() {
#ifdef AIRPORT_WCL
    uint32_t status = 0;
    postMessage(mt7902::wcl::kMsgScanDone, &status, sizeof(status), false);
#else
    UInt32 msg = 0;
    postMessage(APPLE80211_M_SCAN_DONE, &msg, sizeof(msg), false);
#endif
}

void AirportMT7902SkywalkInterface::notifyBssDiscovered(const mt7902::ScannedBss &bss) {
#ifdef AIRPORT_WCL
    memset(mWclBeaconMsg, 0, sizeof(mWclBeaconMsg));
    auto *meta = reinterpret_cast<mt7902::wcl::BeaconMetadata *>(mWclBeaconMsg);
    size_t ieLen = bss.ieLen > 0x7ff ? 0x7ff : bss.ieLen;
    meta->ieLength = static_cast<uint32_t>(ieLen);
    meta->channelSpec = static_cast<uint16_t>(
        bss.channel | (bss.band == mt7902::kBand5GHz ? 0xd000U : 0x1000U));
    memcpy(meta->ssid, bss.ssid, bss.ssidLen);
    meta->ssidLength = bss.ssidLen;
    meta->channel = bss.channel;
    memcpy(meta->bssid, bss.bssid, 6);
    meta->rssi = bss.rssi;
    meta->noise = static_cast<int16_t>(bss.noise);
    meta->snr = static_cast<int16_t>(bss.rssi - bss.noise);
    meta->interval = bss.beaconInterval;
    meta->capability = bss.capability;
    meta->flags = mt7902::wcl::kRssiValid | (bss.ssidLen ? mt7902::wcl::kSsidPresent : 0);
    if (ieLen > 0)
        memcpy(mWclBeaconMsg + sizeof(*meta), bss.ies, ieLen);

    postMessage(mt7902::wcl::kMsgScanResult, mWclBeaconMsg, sizeof(*meta) + ieLen, false);
#else
    (void)bss;
#endif
}

void AirportMT7902SkywalkInterface::notifyAssocDone(bool success, uint16_t statusCode) {
#ifdef AIRPORT_WCL
    const auto &lk = mSta->link();
    mt7902::wcl::AssociationEvent ev = {};
    ev.status = statusCode;
    ev.reason = 0;
    memcpy(ev.bssid, lk.bssid, 6);
    ev.driverStatus = success ? 0 : -1;
    postMessage(mt7902::wcl::kMsgAssocComplete, &ev, sizeof(ev), false);

    if (success) {
        mWclFirstBeaconSent = false;
        mt7902::wcl::ConnectionEvent conn = {};
        conn.status = 0;
        memcpy(conn.peers[0].bssid, lk.bssid, 6);
        postMessage(mt7902::wcl::kMsgConnectComplete, &conn, sizeof(conn), false);
    }
#else
    (void)success;
    (void)statusCode;
    postMessage(APPLE80211_M_ASSOC_DONE, nullptr, 0, false);
#endif
}

void AirportMT7902SkywalkInterface::notifyLinkChanged(bool up) {
#ifdef AIRPORT_WCL
    const auto &lk = mSta->link();
    mt7902::wcl::LinkStatusEvent ev = {};
    memcpy(ev.bssid, lk.bssid, 6);
    ev.up = up ? 1 : 0;
    ev.reason = up ? 0 : lk.lastDeauthReason;
    postMessage(mt7902::wcl::kMsgLinkStatus, &ev, sizeof(ev), false);
#else
    (void)up;
    postMessage(APPLE80211_M_LINK_CHANGED, nullptr, 0, false);
#endif
}

void AirportMT7902SkywalkInterface::notifyDeauth(uint16_t reasonCode, bool isDisassoc) {
#ifdef AIRPORT_WCL
    const auto &lk = mSta->link();
    mt7902::wcl::DeauthEvent ev = {};
    ev.reason = reasonCode;
    memcpy(ev.bssid, lk.bssid, 6);
    ev.disassociation = isDisassoc ? 1 : 0;
    postMessage(mt7902::wcl::kMsgDeauth, &ev, sizeof(ev), false);
#else
    (void)reasonCode;
    (void)isDisassoc;
    postMessage(APPLE80211_M_DEAUTH_RECEIVED, nullptr, 0, false);
#endif
}

void AirportMT7902SkywalkInterface::notifyBeacon(const mt7902::ScannedBss &bss) {
#ifdef AIRPORT_WCL
    if (!mWclFirstBeaconSent) {
        mWclFirstBeaconSent = true;
        mt7902::wcl::FirstBeaconEvent fb = {};
        memcpy(fb.peer.bssid, bss.bssid, 6);
        postMessage(mt7902::wcl::kMsgFirstBeacon, &fb, sizeof(fb), false);
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
    if (!mSta) return kIOReturnNotReady;
    memset(cd, 0, sizeof(*cd));
    cd->version = APPLE80211_VERSION;
    cd->channel.version = APPLE80211_VERSION;
    cd->channel.channel = mSta->link().channel;
    cd->channel.flags =
        (mSta->link().band == mt7902::kBand5GHz ? mt7902::kAppleChanFlag5Ghz : mt7902::kAppleChanFlag2Ghz) |
        mt7902::kAppleChanFlag20Mhz | mt7902::kAppleChanFlagActive;
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
    st->version = APPLE80211_VERSION;
    switch (mSta->link().state) {
    case mt7902::kStaScanning:       st->state = APPLE80211_S_SCAN; break;
    case mt7902::kStaAuthenticating: st->state = APPLE80211_S_AUTH; break;
    case mt7902::kStaAssociating:    st->state = APPLE80211_S_ASSOC; break;
    case mt7902::kStaAssociated:
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
    size_t n = mt7902::getSupportedChannels(cap.has2Ghz, cap.has5Ghz, cap.has6Ghz,
                                            list, APPLE80211_MAX_CHANNELS);
    ch->num_channels = static_cast<uint32_t>(n);
    for (size_t i = 0; i < n; ++i) {
        ch->supported_channels[i].version = APPLE80211_VERSION;
        ch->supported_channels[i].channel = list[i].channel;
        ch->supported_channels[i].flags = mt7902::channelToAppleFlags(
            list[i], list[i].allow80 ? mt7902::CMD_CBW_80MHZ : mt7902::CMD_CBW_20MHZ);
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
    memset(rsn, 0, sizeof(*rsn));
    rsn->version = APPLE80211_VERSION;
    rsn->len = mSta->link().rsnIeLen;
    if (rsn->len > 0)
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
    memset(mv, 0, sizeof(*mv));
    mv->version = APPLE80211_VERSION;
    mv->index = mSta->link().mcs;
    mv->nss = mSta->link().nss;
    mv->bw = 80;
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

IOReturn AirportMT7902SkywalkInterface::setAUTH_TYPE(apple80211_authtype_data *ad) {
    (void)ad;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setCIPHER_KEY(apple80211_key *key) {
    if (!mSta || !key) return kIOReturnBadArgument;
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
    return kIOReturnSuccess;
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
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getWCL_CHANNELS_INFO(apple80211ChannelInfo *out) {
    if (!mSta || !out) return kIOReturnBadArgument;
    auto *info = reinterpret_cast<mt7902::wcl::ChannelInfo *>(out);
    memset(info, 0, sizeof(*info));
    memcpy(info->country, mSta->countryCode(), 2);
    info->supports6GHz = mSta->device().capability().has6Ghz ? 1 : 0;

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
    }
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getWCL_BSS_INFO(apple80211_beacon_msg *out) {
    if (!mSta || !out || mSta->link().state < mt7902::kStaAssociated)
        return kIOReturnNotFound;
    memcpy(out, mWclBeaconMsg, sizeof(mt7902::wcl::BeaconMetadata));
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *out) {
    if (!mSta || !out || mSta->link().state < mt7902::kStaAssociated)
        return kIOReturnNotFound;
    memset(out, 0, 64);
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

IOReturn AirportMT7902SkywalkInterface::getBEACON_INFO(apple80211_beacon_info_t *out) {
    if (!mSta || !out || mSta->link().state < mt7902::kStaAssociated)
        return kIOReturnNotFound;
    memset(out, 0, 64);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setWCL_SCAN_REQ(apple80211ScanRequest *rawReq) {
    if (!mSta || !rawReq) return kIOReturnBadArgument;
    mt7902::wcl::DecodedScanRequest decoded;
    mt7902::ScanRequestParams params;
    if (mt7902::wcl::decodeScan(rawReq, 0x58 + 64 * 12, decoded)) {
        params.active = true;
        if (decoded.ssidLength > 0 && decoded.ssidLength <= 32) {
            params.ssidCount = 1;
            params.ssidLens[0] = static_cast<uint8_t>(decoded.ssidLength);
            memcpy(params.ssids[0], decoded.ssid, decoded.ssidLength);
        }
    }
    return (mSta->startScan(params) == mt7902::kOk) ? kIOReturnSuccess : kIOReturnBusy;
}

IOReturn AirportMT7902SkywalkInterface::setWCL_SCAN_ABORT(void *) {
    if (mSta) (void)mSta->abortScan();
    return kIOReturnSuccess;
}

IOReturn AirportMT7902SkywalkInterface::setWCL_ASSOCIATE(apple80211AssocCandidates *rawCand) {
    if (!mSta || !rawCand) return kIOReturnBadArgument;
    mt7902::wcl::DecodedJoinRequest join;
    if (!mt7902::wcl::decodeJoin(rawCand, 0x218 + 18, join))
        return kIOReturnBadArgument;

    mt7902::AssocRequestParams req;
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
    return (mSta->associate(req) == mt7902::kOk) ? kIOReturnSuccess : kIOReturnError;
}

IOReturn AirportMT7902SkywalkInterface::setWCL_LEAVE_NETWORK(apple80211_leave_network *) {
    if (mSta) (void)mSta->disassociate(8);
    return kIOReturnSuccess;
}

#if __IO80211_TARGET == __MAC_15_2
IOReturn AirportMT7902SkywalkInterface::setWCL_JOIN_ABORT(void *) {
    if (mSta) (void)mSta->disassociate(3);
    uint32_t status = 0;
    postMessage(mt7902::wcl::kMsgJoinAbortComplete, &status, sizeof(status), false);
    return kIOReturnSuccess;
}
#else
IOReturn AirportMT7902SkywalkInterface::setWCL_JOIN_ABORT(apple80211_wcl_abort_join *) {
    if (mSta) (void)mSta->disassociate(3);
    uint32_t status = 0;
    postMessage(mt7902::wcl::kMsgJoinAbortComplete, &status, sizeof(status), false);
    return kIOReturnSuccess;
}
#endif

#endif // AIRPORT_WCL

#endif // KERNEL && IO80211FAMILY_V2
