// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#if defined(KERNEL) && !defined(IO80211FAMILY_V2)

#include "AirportMT7902ControllerV1.hpp"
#include "../Net80211/MT7902Channels.hpp"
#include <sys/kpi_mbuf.h>

#define super IO80211Controller
OSDefineMetaClassAndStructors(AirportMT7902, IO80211Controller);

static IOPMPowerState kPowerStates[2] = {
    { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static uint8_t appleCipherToMcu(uint32_t cipher) {
    switch (cipher) {
    case APPLE80211_CIPHER_WEP_40:  return mt7902::MCU_CIPHER_WEP40;
    case APPLE80211_CIPHER_WEP_104: return mt7902::MCU_CIPHER_WEP104;
    case APPLE80211_CIPHER_TKIP:    return mt7902::MCU_CIPHER_TKIP;
    case APPLE80211_CIPHER_AES_OCB:
    case APPLE80211_CIPHER_AES_CCM: return mt7902::MCU_CIPHER_AES_CCMP;
    default:                        return mt7902::MCU_CIPHER_NONE;
    }
}

bool AirportMT7902::init(OSDictionary *properties) {
    if (!super::init(properties))
        return false;
    return true;
}

void AirportMT7902::free() {
    releaseResources();
    super::free();
}

IOService *AirportMT7902::probe(IOService *provider, SInt32 *score) {
    IOService *res = super::probe(provider, score);
    if (!res)
        return nullptr;

    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci)
        return nullptr;

    UInt16 vid = pci->configRead16(kIOPCIConfigVendorID);
    UInt16 did = pci->configRead16(kIOPCIConfigDeviceID);
    if (vid != mt7902::kMT7902VendorID || did != mt7902::kMT7902DeviceID)
        return nullptr;

    MTK_LOG("AirportMT7902 (V1) probed MediaTek MT7902 (%04x:%04x)", vid, did);
    return this;
}

bool AirportMT7902::createWorkLoop() {
    if (!mWorkLoop)
        mWorkLoop = IO80211WorkLoop::workLoop();
    return mWorkLoop != nullptr;
}

IOWorkLoop *AirportMT7902::getWorkLoop() const {
    return mWorkLoop;
}

bool AirportMT7902::createMediumTables(const IONetworkMedium **primary) {
    mMediumDict = OSDictionary::withCapacity(2);
    if (!mMediumDict)
        return false;

    IONetworkMedium *autoMed = IONetworkMedium::medium(kIOMediumIEEE80211Auto, 1200000000ULL);
    if (!autoMed)
        return false;
    IONetworkMedium::addMedium(mMediumDict, autoMed);
    if (primary)
        *primary = autoMed;
    autoMed->release();

    IONetworkMedium *noneMed = IONetworkMedium::medium(kIOMediumIEEE80211None, 0);
    if (noneMed) {
        IONetworkMedium::addMedium(mMediumDict, noneMed);
        noneMed->release();
    }
    return publishMediumDictionary(mMediumDict);
}

bool AirportMT7902::start(IOService *provider) {
    mPciNub = OSDynamicCast(IOPCIDevice, provider);
    if (!mPciNub || !super::start(provider))
        return false;

    if (!mPciNub->open(this)) {
        super::stop(provider);
        return false;
    }

    mBus = new mt7902::MT7902IOKitBus();
    if (!mBus || !mBus->attach(mPciNub)) {
        MTK_ERR("Failed to attach MT7902 PCIe BAR0 bus");
        releaseResources();
        return false;
    }

    mDevice = new mt7902::MT7902Device(*mBus);
    if (!mDevice || mDevice->probeAndInit() != mt7902::kOk) {
        MTK_ERR("MT7902 hardware initialization / firmware boot failed");
        releaseResources();
        return false;
    }

    mSta = new mt7902::MT7902Sta(*mDevice);
    if (!mSta) {
        releaseResources();
        return false;
    }
    mSta->setListener(this);

    mCommandGate = IOCommandGate::commandGate(this);
    if (!mCommandGate || mWorkLoop->addEventSource(mCommandGate) != kIOReturnSuccess) {
        releaseResources();
        return false;
    }

    // Prefer MSI interrupt vector if available, else legacy INTx
    int msiIndex = 0;
    for (int idx = 0; ; ++idx) {
        int intrType = 0;
        if (mPciNub->getInterruptType(idx, &intrType) != kIOReturnSuccess)
            break;
        if (intrType & kIOInterruptTypePCIMessaged) {
            msiIndex = idx;
            break;
        }
    }

    mInterruptSrc = IOInterruptEventSource::interruptEventSource(
        this, &AirportMT7902::interruptOccurred, mPciNub, msiIndex);
    if (!mInterruptSrc || mWorkLoop->addEventSource(mInterruptSrc) != kIOReturnSuccess) {
        releaseResources();
        return false;
    }
    mInterruptSrc->enable();

    mWatchdogTimer = IOTimerEventSource::timerEventSource(this, &AirportMT7902::watchdogFired);
    if (mWatchdogTimer && mWorkLoop->addEventSource(mWatchdogTimer) == kIOReturnSuccess)
        mWatchdogTimer->setTimeoutMS(1000);

    const IONetworkMedium *primary = nullptr;
    if (!createMediumTables(&primary) || !setCurrentMedium(primary)) {
        releaseResources();
        return false;
    }

    if (!attachInterface(reinterpret_cast<IONetworkInterface **>(&mNetIf), true)) {
        MTK_ERR("Failed to attach native IO80211Interface");
        releaseResources();
        return false;
    }

    registerService();
    MTK_LOG("AirportMT7902 (V1) started successfully");
    return true;
}

void AirportMT7902::stop(IOService *provider) {
    if (mNetIf) {
        detachInterface(mNetIf, true);
        OSSafeReleaseNULL(mNetIf);
    }
    releaseResources();
    super::stop(provider);
}

void AirportMT7902::releaseResources() {
    if (mWorkLoop) {
        if (mWatchdogTimer) {
            mWatchdogTimer->cancelTimeout();
            mWorkLoop->removeEventSource(mWatchdogTimer);
            OSSafeReleaseNULL(mWatchdogTimer);
        }
        if (mInterruptSrc) {
            mInterruptSrc->disable();
            mWorkLoop->removeEventSource(mInterruptSrc);
            OSSafeReleaseNULL(mInterruptSrc);
        }
        if (mCommandGate) {
            mWorkLoop->removeEventSource(mCommandGate);
            OSSafeReleaseNULL(mCommandGate);
        }
    }
    if (mSta) {
        mSta->setListener(nullptr);
        delete mSta;
        mSta = nullptr;
    }
    if (mDevice) {
        mDevice->shutdown();
        delete mDevice;
        mDevice = nullptr;
    }
    if (mBus) {
        mBus->detach();
        delete mBus;
        mBus = nullptr;
    }
    if (mPciNub && mPciNub->isOpen(this)) {
        mPciNub->close(this);
    }
    OSSafeReleaseNULL(mMediumDict);
    OSSafeReleaseNULL(mWorkLoop);
}

IONetworkInterface *AirportMT7902::createInterface() {
    AirportMT7902Interface *iface = new AirportMT7902Interface();
    if (!iface)
        return nullptr;
    if (!iface->init(this)) {
        iface->release();
        return nullptr;
    }
    return iface;
}

bool AirportMT7902::configureInterface(IONetworkInterface *netif) {
    if (!super::configureInterface(netif))
        return false;
    IONetworkData *data = netif->getNetworkData(kIONetworkStatsKey);
    if (data)
        mNetStats = static_cast<IONetworkStats *>(data->getBuffer());
    return true;
}

IOReturn AirportMT7902::enable(IONetworkInterface *netif) {
    (void)netif;
    if (!mSta)
        return kIOReturnNotReady;
    if (mSta->setPower(true) != mt7902::kOk)
        return kIOReturnError;
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::disable(IONetworkInterface *netif) {
    (void)netif;
    if (mSta)
        (void)mSta->setPower(false);
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}

UInt32 AirportMT7902::outputPacket(mbuf_t m, void *param) {
    (void)param;
    if (!m || !mSta) {
        if (m) freePacket(m);
        return kIOReturnOutputDropped;
    }

    size_t pktLen = mbuf_pkthdr_len(m);
    if (pktLen < mt7902::kEthHdrLen || pktLen > 2048) {
        freePacket(m);
        return kIOReturnOutputDropped;
    }

    uint8_t linear[2048];
    if (mbuf_copydata(m, 0, pktLen, linear) != 0) {
        freePacket(m);
        return kIOReturnOutputDropped;
    }
    freePacket(m);

    int err = mSta->outputEthernetPacket(linear, pktLen);
    if (err != mt7902::kOk) {
        if (mNetStats) mNetStats->outputErrors++;
        return kIOReturnOutputDropped;
    }
    if (mNetStats) mNetStats->outputPackets++;
    return kIOReturnOutputSuccess;
}

IOReturn AirportMT7902::getHardwareAddress(IOEthernetAddress *addrP) {
    if (!addrP || !mDevice)
        return kIOReturnBadArgument;
    memcpy(addrP->bytes, mDevice->capability().macAddr, 6);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::setHardwareAddress(const IOEthernetAddress *addrP) {
    (void)addrP;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::getHardwareAddressForInterface(IO80211Interface *netif, IOEthernetAddress *addr) {
    (void)netif;
    return getHardwareAddress(addr);
}

IOReturn AirportMT7902::getPacketFilters(const OSSymbol *group, UInt32 *filters) const {
    if (group == gIONetworkFilterGroup) {
        *filters = kIOPacketFilterUnicast | kIOPacketFilterBroadcast |
                   kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
        return kIOReturnSuccess;
    }
    return super::getPacketFilters(group, filters);
}

IOReturn AirportMT7902::setPromiscuousMode(bool active) {
    (void)active;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::setMulticastMode(bool active) {
    (void)active;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::setMulticastList(IOEthernetAddress *addrs, UInt32 count) {
    (void)addrs;
    (void)count;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::selectMedium(const IONetworkMedium *medium) {
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

UInt32 AirportMT7902::getFeatures() const {
    return kIONetworkFeatureMultiPages;
}

const OSString *AirportMT7902::newVendorString() const {
    return OSString::withCString("MediaTek");
}

const OSString *AirportMT7902::newModelString() const {
    return OSString::withCString("MT7902 Wi-Fi 6E (Filogic 310)");
}

IOReturn AirportMT7902::registerWithPolicyMaker(IOService *policyMaker) {
    return policyMaker->registerPowerDriver(this, kPowerStates, 2);
}

IOReturn AirportMT7902::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker) {
    (void)policyMaker;
    mPmState = static_cast<uint32_t>(powerStateOrdinal);
    if (mDevice) {
        if (powerStateOrdinal == 0)
            (void)mDevice->suspend();
        else
            (void)mDevice->resume();
    }
    return IOPMAckImplied;
}

SInt32 AirportMT7902::stopDMA() {
    if (mDevice)
        (void)mDevice->dma().disable(false);
    return kIOReturnSuccess;
}

UInt32 AirportMT7902::hardwareOutputQueueDepth(IO80211Interface *interface) {
    (void)interface;
    return mt7902::MT7902_TX_RING_SIZE;
}

SInt32 AirportMT7902::performCountryCodeOperation(IO80211Interface *interface, IO80211CountryCodeOp op) {
    (void)interface;
    if (op == kIO80211CountryCodeReset && mSta)
        (void)mSta->setCountryCode("00");
    return kIOReturnSuccess;
}

SInt32 AirportMT7902::enableFeature(IO80211FeatureCode code, void *data) {
    (void)code;
    (void)data;
    return kIOReturnSuccess;
}

bool AirportMT7902::useAppleRSNSupplicant(IO80211Interface *interface) {
    (void)interface;
    return true;
}

int AirportMT7902::outputRaw80211Packet(IO80211Interface *interface, mbuf_t m) {
    (void)interface;
    if (!m || !mDevice) {
        if (m) freePacket(m);
        return kIOReturnBadArgument;
    }
    size_t len = mbuf_pkthdr_len(m);
    if (len > 2048) {
        freePacket(m);
        return kIOReturnMessageTooLarge;
    }
    uint8_t buf[2048];
    mbuf_copydata(m, 0, len, buf);
    freePacket(m);
    return (mDevice->send80211Frame(buf, len, 0, false) == mt7902::kOk)
               ? kIOReturnSuccess
               : kIOReturnError;
}

SInt32 AirportMT7902::monitorModeSetEnabled(IO80211Interface *interface, bool enabled, UInt32 dlt) {
    (void)interface;
    (void)enabled;
    (void)dlt;
    return kIOReturnSuccess;
}

void AirportMT7902::interruptOccurred(OSObject *owner, IOInterruptEventSource *src, int count) {
    (void)src;
    (void)count;
    AirportMT7902 *self = OSDynamicCast(AirportMT7902, owner);
    if (self && self->mDevice)
        self->mDevice->serviceInterrupt();
}

void AirportMT7902::watchdogFired(OSObject *owner, IOTimerEventSource *timer) {
    AirportMT7902 *self = OSDynamicCast(AirportMT7902, owner);
    if (!self)
        return;
    if (self->mDevice)
        self->mDevice->serviceInterrupt();
    timer->setTimeoutMS(1000);
}

// --- IStaEventListener callbacks ---

void AirportMT7902::onDataFrameReceived(const uint8_t *ethFrame, size_t len) {
    if (!mNetIf || !ethFrame || len < mt7902::kEthHdrLen)
        return;
    mbuf_t m = allocatePacket(static_cast<UInt32>(len));
    if (!m)
        return;
    if (mbuf_copyback(m, 0, len, ethFrame, MBUF_DONTWAIT) != 0) {
        freePacket(m);
        return;
    }
    mNetIf->inputPacket(m, static_cast<UInt32>(len));
    if (mNetStats) mNetStats->inputPackets++;
}

void AirportMT7902::onScanFinished() {
    if (mNetIf) {
        UInt32 msg = 0;
        mNetIf->postMessage(APPLE80211_M_SCAN_DONE, &msg, sizeof(msg));
    }
}

void AirportMT7902::onBssDiscovered(const mt7902::ScannedBss &bss) {
    (void)bss;
}

void AirportMT7902::onAssociationCompleted(bool success, uint16_t statusCode) {
    (void)statusCode;
    if (!mNetIf)
        return;
    mNetIf->postMessage(APPLE80211_M_ASSOC_DONE);
    if (success) {
        mNetIf->postMessage(APPLE80211_M_BSSID_CHANGED);
        mNetIf->postMessage(APPLE80211_M_SSID_CHANGED);
    }
}

void AirportMT7902::onLinkStateChanged(bool up) {
    if (up) {
        UInt64 speed = static_cast<UInt64>(mSta->link().txRateMbps) * 1000000ULL;
        setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, getCurrentMedium(), speed);
    } else {
        setLinkStatus(kIONetworkLinkValid, getCurrentMedium(), 0);
    }
    if (mNetIf)
        mNetIf->postMessage(APPLE80211_M_LINK_CHANGED);
}

void AirportMT7902::onDeauthenticated(uint16_t reasonCode, bool isDisassoc) {
    (void)reasonCode;
    if (mNetIf) {
        mNetIf->postMessage(isDisassoc ? APPLE80211_M_DISASSOC_RECEIVED : APPLE80211_M_DEAUTH_RECEIVED);
        mNetIf->postMessage(APPLE80211_M_LINK_CHANGED);
    }
}

void AirportMT7902::onAssociatedBeacon(const mt7902::ScannedBss &bss) {
    (void)bss;
}

// --- Native Apple80211 IOCTLs ---

IOReturn AirportMT7902::handleGetCardCapabilities(apple80211_capability_data *cap) {
    memset(cap, 0, sizeof(*cap));
    cap->version = APPLE80211_VERSION;
    uint32_t caps = 0;
    caps |= (1U << APPLE80211_CAP_WEP);
    caps |= (1U << APPLE80211_CAP_TKIP);
    caps |= (1U << APPLE80211_CAP_AES);
    caps |= (1U << APPLE80211_CAP_AES_CCM);
    caps |= (1U << APPLE80211_CAP_WPA);
    caps |= (1U << APPLE80211_CAP_WPA1);
    caps |= (1U << APPLE80211_CAP_WPA2);
    caps |= (1U << APPLE80211_CAP_TKIPMIC);
    caps |= (1U << APPLE80211_CAP_WME);
    caps |= (1U << APPLE80211_CAP_SHORT_GI_20MHZ);
    caps |= (1U << APPLE80211_CAP_SHORT_GI_40MHZ);
    caps |= (1U << APPLE80211_CAP_TXSPATIAL);
    caps |= (1U << APPLE80211_CAP_RXSPATIAL);
    cap->capabilities[0] = static_cast<uint8_t>(caps & 0xffU);
    cap->capabilities[1] = static_cast<uint8_t>((caps >> 8) & 0xffU);
    cap->capabilities[2] = static_cast<uint8_t>((caps >> 16) & 0xffU);
    cap->capabilities[3] = static_cast<uint8_t>((caps >> 24) & 0xffU);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::handleGetSupportedChannels(apple80211_sup_channel_data *ch) {
    memset(ch, 0, sizeof(*ch));
    ch->version = APPLE80211_VERSION;
    const auto &cap = mDevice->capability();
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

IOReturn AirportMT7902::handleGetScanResult(apple80211_scan_result **out) {
    const mt7902::ScannedBss *bss = mSta->scanCache().nextResult();
    if (!bss)
        return static_cast<IOReturn>(0xe0820446); // APPLE80211_E_SCAN_DONE (end of list)

    memset(&mScanResultScratch, 0, sizeof(mScanResultScratch));
    mScanResultScratch.version = APPLE80211_VERSION;
    mScanResultScratch.asr_channel.version = APPLE80211_VERSION;
    mScanResultScratch.asr_channel.channel = bss->channel;
    mScanResultScratch.asr_channel.flags =
        (bss->band == mt7902::kBand5GHz ? mt7902::kAppleChanFlag5Ghz : mt7902::kAppleChanFlag2Ghz) |
        mt7902::kAppleChanFlag20Mhz | mt7902::kAppleChanFlagActive;
    mScanResultScratch.asr_noise = static_cast<int8_t>(bss->noise);
    mScanResultScratch.asr_rssi = static_cast<int8_t>(bss->rssi);
    mScanResultScratch.asr_beacon_int = bss->beaconInterval;
    mScanResultScratch.asr_cap = bss->capability;
    memcpy(mScanResultScratch.asr_bssid, bss->bssid, 6);
    mScanResultScratch.asr_ssid_len = bss->ssidLen;
    memcpy(mScanResultScratch.asr_ssid, bss->ssid, bss->ssidLen);
    if (bss->ieLen > 0) {
        memcpy(mScanIeScratch, bss->ies, bss->ieLen);
        mScanResultScratch.asr_ie_len = bss->ieLen;
        mScanResultScratch.asr_ie_data = mScanIeScratch;
    }
    *out = &mScanResultScratch;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::handleSetScanReq(apple80211_scan_data *req) {
    mt7902::ScanRequestParams params;
    params.active = (req->scan_type != APPLE80211_SCAN_TYPE_PASSIVE);
    if (req->ssid_len > 0 && req->ssid_len <= 32) {
        params.ssidCount = 1;
        params.ssidLens[0] = static_cast<uint8_t>(req->ssid_len);
        memcpy(params.ssids[0], req->ssid, req->ssid_len);
    }
    if (req->num_channels > 0 && req->num_channels <= 64) {
        params.channelCount = static_cast<uint8_t>(req->num_channels);
        for (uint32_t i = 0; i < req->num_channels; ++i) {
            uint8_t ch = static_cast<uint8_t>(req->channels[i].channel);
            params.channels[i].channel_num = ch;
            params.channels[i].band = (req->channels[i].flags & mt7902::kAppleChanFlag6Ghz)
                                          ? 3
                                          : ((req->channels[i].flags & mt7902::kAppleChanFlag5Ghz) || ch > 14 ? 2 : 1);
        }
    }
    return (mSta->startScan(params) == mt7902::kOk) ? kIOReturnSuccess : kIOReturnBusy;
}

IOReturn AirportMT7902::handleSetScanReqMultiple(apple80211_scan_multiple_data *req) {
    mt7902::ScanRequestParams params;
    params.active = (req->scan_type != APPLE80211_SCAN_TYPE_PASSIVE);
    uint8_t sc = req->ssid_count > 4 ? 4 : static_cast<uint8_t>(req->ssid_count);
    for (uint8_t i = 0; i < sc; ++i) {
        if (req->ssids[i].ssid_len > 0 && req->ssids[i].ssid_len <= 32) {
            params.ssidLens[params.ssidCount] = static_cast<uint8_t>(req->ssids[i].ssid_len);
            memcpy(params.ssids[params.ssidCount], req->ssids[i].ssid_bytes, req->ssids[i].ssid_len);
            params.ssidCount++;
        }
    }
    return (mSta->startScan(params) == mt7902::kOk) ? kIOReturnSuccess : kIOReturnBusy;
}

IOReturn AirportMT7902::handleSetAssociate(apple80211_assoc_data *req) {
    mt7902::AssocRequestParams p;
    p.ssidLen = req->ad_ssid_len > 32 ? 32 : static_cast<uint8_t>(req->ad_ssid_len);
    memcpy(p.ssid, req->ad_ssid, p.ssidLen);
    bool bssidNonZero = false;
    for (int i = 0; i < 6; ++i) bssidNonZero |= (req->ad_bssid.octet[i] != 0);
    if (bssidNonZero) {
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

IOReturn AirportMT7902::handleSetCipherKey(apple80211_key *key) {
    if (!key)
        return kIOReturnBadArgument;
    bool pairwise = (key->key_flags & APPLE80211_KEY_FLAG_UNICAST) != 0;
    uint8_t cipher = appleCipherToMcu(key->key_cipher_type);
    bool set = (key->key_len > 0 && cipher != mt7902::MCU_CIPHER_NONE);
    int err = mSta->installCipherKey(cipher, static_cast<uint8_t>(key->key_index & 0x3U),
                                     key->key, key->key_len, pairwise, set);
    return (err == mt7902::kOk) ? kIOReturnSuccess : kIOReturnError;
}

SInt32 AirportMT7902::apple80211Request(unsigned int requestType, int requestNumber,
                                        IO80211Interface *interface, void *data) {
    (void)interface;
    if (!mSta || !mDevice)
        return kIOReturnNotReady;
    if (requestType != SIOCGA80211 && requestType != SIOCSA80211)
        return kIOReturnBadArgument;

    const auto &lk = mSta->link();

    switch (requestNumber) {
    case APPLE80211_IOC_SSID:
        if (requestType == SIOCGA80211) {
            if (lk.state < mt7902::kStaAssociated) return 6;
            auto *sd = static_cast<apple80211_ssid_data *>(data);
            memset(sd, 0, sizeof(*sd));
            sd->version = APPLE80211_VERSION;
            sd->ssid_len = lk.ssidLen;
            memcpy(sd->ssid_bytes, lk.ssid, lk.ssidLen);
            return kIOReturnSuccess;
        }
        return kIOReturnSuccess;

    case APPLE80211_IOC_BSSID:
        if (requestType == SIOCGA80211) {
            if (lk.state < mt7902::kStaAssociated) return 6;
            auto *bd = static_cast<apple80211_bssid_data *>(data);
            memset(bd, 0, sizeof(*bd));
            bd->version = APPLE80211_VERSION;
            memcpy(bd->bssid.octet, lk.bssid, 6);
            return kIOReturnSuccess;
        }
        return kIOReturnSuccess;

    case APPLE80211_IOC_CHANNEL:
        if (requestType == SIOCGA80211) {
            auto *cd = static_cast<apple80211_channel_data *>(data);
            memset(cd, 0, sizeof(*cd));
            cd->version = APPLE80211_VERSION;
            cd->channel.version = APPLE80211_VERSION;
            cd->channel.channel = lk.channel;
            cd->channel.flags =
                (lk.band == mt7902::kBand5GHz ? mt7902::kAppleChanFlag5Ghz : mt7902::kAppleChanFlag2Ghz) |
                mt7902::kAppleChanFlag20Mhz | mt7902::kAppleChanFlagActive;
            return kIOReturnSuccess;
        }
        return kIOReturnSuccess;

    case APPLE80211_IOC_POWER: {
        auto *pd = static_cast<apple80211_power_data *>(data);
        if (requestType == SIOCGA80211) {
            memset(pd, 0, sizeof(*pd));
            pd->version = APPLE80211_VERSION;
            pd->num_radios = 1;
            pd->power_state[0] = mSta->isPoweredOn() ? APPLE80211_POWER_ON : APPLE80211_POWER_OFF;
            return kIOReturnSuccess;
        } else {
            bool on = (pd->num_radios > 0 && pd->power_state[0] != APPLE80211_POWER_OFF);
            int err = mSta->setPower(on);
            if (err == mt7902::kOk && mNetIf)
                mNetIf->postMessage(APPLE80211_M_POWER_CHANGED);
            return (err == mt7902::kOk) ? kIOReturnSuccess : kIOReturnError;
        }
    }

    case APPLE80211_IOC_STATE:
        if (requestType == SIOCGA80211) {
            auto *st = static_cast<apple80211_state_data *>(data);
            st->version = APPLE80211_VERSION;
            switch (lk.state) {
            case mt7902::kStaScanning:       st->state = APPLE80211_S_SCAN; break;
            case mt7902::kStaAuthenticating: st->state = APPLE80211_S_AUTH; break;
            case mt7902::kStaAssociating:    st->state = APPLE80211_S_ASSOC; break;
            case mt7902::kStaAssociated:
            case mt7902::kStaRun:            st->state = APPLE80211_S_RUN; break;
            default:                         st->state = APPLE80211_S_INIT; break;
            }
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_OP_MODE:
        if (requestType == SIOCGA80211) {
            auto *om = static_cast<apple80211_opmode_data *>(data);
            om->version = APPLE80211_VERSION;
            om->op_mode = APPLE80211_M_STA;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_PHY_MODE:
        if (requestType == SIOCGA80211) {
            auto *pm = static_cast<apple80211_phymode_data *>(data);
            pm->version = APPLE80211_VERSION;
            pm->phy_mode = APPLE80211_MODE_11A | APPLE80211_MODE_11B |
                           APPLE80211_MODE_11G | APPLE80211_MODE_11N |
                           APPLE80211_MODE_11AC | APPLE80211_MODE_11AX;
            pm->active_phy_mode = APPLE80211_MODE_11AX;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_CARD_CAPABILITIES:
        if (requestType == SIOCGA80211)
            return handleGetCardCapabilities(static_cast<apple80211_capability_data *>(data));
        break;

    case APPLE80211_IOC_SUPPORTED_CHANNELS:
    case APPLE80211_IOC_HW_SUPPORTED_CHANNELS:
        if (requestType == SIOCGA80211)
            return handleGetSupportedChannels(static_cast<apple80211_sup_channel_data *>(data));
        break;

    case APPLE80211_IOC_SCAN_REQ:
        if (requestType == SIOCSA80211)
            return handleSetScanReq(static_cast<apple80211_scan_data *>(data));
        break;

    case APPLE80211_IOC_SCAN_REQ_MULTIPLE:
        if (requestType == SIOCSA80211)
            return handleSetScanReqMultiple(static_cast<apple80211_scan_multiple_data *>(data));
        break;

    case APPLE80211_IOC_SCAN_RESULT:
        if (requestType == SIOCGA80211)
            return handleGetScanResult(static_cast<apple80211_scan_result **>(data));
        break;

    case APPLE80211_IOC_SCANCACHE_CLEAR:
        if (requestType == SIOCSA80211) {
            mSta->scanCache().clear();
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_ASSOCIATE:
        if (requestType == SIOCSA80211)
            return handleSetAssociate(static_cast<apple80211_assoc_data *>(data));
        break;

    case APPLE80211_IOC_ASSOCIATE_RESULT:
        if (requestType == SIOCGA80211) {
            auto *ar = static_cast<apple80211_assoc_result_data *>(data);
            memset(ar, 0, sizeof(*ar));
            ar->version = APPLE80211_VERSION;
            ar->result = (lk.state >= mt7902::kStaAssociated)
                             ? APPLE80211_RESULT_SUCCESS
                             : APPLE80211_RESULT_UNAVAILABLE;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_ASSOCIATION_STATUS:
        if (requestType == SIOCGA80211) {
            auto *as = static_cast<apple80211_assoc_status_data *>(data);
            as->version = APPLE80211_VERSION;
            as->status = lk.lastAssocStatus;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_DISASSOCIATE:
        if (requestType == SIOCSA80211) {
            (void)mSta->disassociate(8);
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_DEAUTH:
        if (requestType == SIOCGA80211) {
            auto *dd = static_cast<apple80211_deauth_data *>(data);
            memset(dd, 0, sizeof(*dd));
            dd->version = APPLE80211_VERSION;
            dd->deauth_reason = lk.lastDeauthReason;
            return kIOReturnSuccess;
        } else {
            (void)mSta->disassociate(3);
            return kIOReturnSuccess;
        }

    case APPLE80211_IOC_AUTH_TYPE:
        if (requestType == SIOCGA80211) {
            auto *ad = static_cast<apple80211_authtype_data *>(data);
            ad->version = APPLE80211_VERSION;
            ad->authtype_lower = lk.authLower;
            ad->authtype_upper = lk.authUpper;
            return kIOReturnSuccess;
        }
        return kIOReturnSuccess;

    case APPLE80211_IOC_CIPHER_KEY:
        if (requestType == SIOCSA80211)
            return handleSetCipherKey(static_cast<apple80211_key *>(data));
        break;

    case APPLE80211_IOC_RSN_IE:
        if (requestType == SIOCGA80211) {
            auto *rsn = static_cast<apple80211_rsn_ie_data *>(data);
            memset(rsn, 0, sizeof(*rsn));
            rsn->version = APPLE80211_VERSION;
            rsn->len = lk.rsnIeLen;
            if (lk.rsnIeLen > 0)
                memcpy(rsn->ie, lk.rsnIe, lk.rsnIeLen);
            return kIOReturnSuccess;
        }
        return kIOReturnSuccess;

    case APPLE80211_IOC_AP_IE_LIST:
        if (requestType == SIOCGA80211) {
            auto *ap = static_cast<apple80211_ap_ie_data *>(data);
            if (!lk.apIeLen) return kIOReturnNotFound;
            uint32_t copyLen = lk.apIeLen < ap->len ? lk.apIeLen : ap->len;
            ap->version = APPLE80211_VERSION;
            ap->len = copyLen;
            if (ap->ie_data && copyLen > 0)
                memcpy(ap->ie_data, lk.apIes, copyLen);
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_RSSI:
        if (requestType == SIOCGA80211) {
            auto *rd = static_cast<apple80211_rssi_data *>(data);
            memset(rd, 0, sizeof(*rd));
            rd->version = APPLE80211_VERSION;
            rd->num_radios = 1;
            rd->rssi_unit = APPLE80211_UNIT_DBM;
            rd->rssi[0] = rd->aggregate_rssi = lk.rssi;
            rd->rssi_ext[0] = rd->aggregate_rssi_ext = lk.rssi;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_NOISE:
        if (requestType == SIOCGA80211) {
            auto *nd = static_cast<apple80211_noise_data *>(data);
            memset(nd, 0, sizeof(*nd));
            nd->version = APPLE80211_VERSION;
            nd->num_radios = 1;
            nd->noise_unit = APPLE80211_UNIT_DBM;
            nd->noise[0] = nd->aggregate_noise = lk.noise;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_RATE:
        if (requestType == SIOCGA80211) {
            auto *rt = static_cast<apple80211_rate_data *>(data);
            memset(rt, 0, sizeof(*rt));
            rt->version = APPLE80211_VERSION;
            rt->num_radios = 1;
            rt->rate[0] = lk.txRateMbps;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_TXPOWER:
        if (requestType == SIOCGA80211) {
            auto *tp = static_cast<apple80211_txpower_data *>(data);
            tp->version = APPLE80211_VERSION;
            tp->txpower_unit = APPLE80211_UNIT_PERCENT;
            tp->txpower = 100;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_MCS:
        if (requestType == SIOCGA80211) {
            auto *md = static_cast<apple80211_mcs_data *>(data);
            md->version = APPLE80211_VERSION;
            md->index = lk.mcs;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_NSS:
        if (requestType == SIOCGA80211) {
            auto *nd = static_cast<apple80211_nss_data *>(data);
            nd->version = APPLE80211_VERSION;
            nd->nss = mDevice->capability().nss;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_COUNTRY_CODE: {
        auto *cc = static_cast<apple80211_country_code_data *>(data);
        if (requestType == SIOCGA80211) {
            memset(cc, 0, sizeof(*cc));
            cc->version = APPLE80211_VERSION;
            memcpy(cc->cc, mSta->countryCode(), 2);
            return kIOReturnSuccess;
        } else {
            (void)mSta->setCountryCode(reinterpret_cast<const char *>(cc->cc));
            if (mNetIf)
                mNetIf->postMessage(APPLE80211_M_COUNTRY_CODE_CHANGED);
            return kIOReturnSuccess;
        }
    }

    case APPLE80211_IOC_LOCALE:
        if (requestType == SIOCGA80211) {
            auto *ld = static_cast<apple80211_locale_data *>(data);
            ld->version = APPLE80211_VERSION;
            ld->locale = APPLE80211_LOCALE_FCC;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_DRIVER_VERSION:
        if (requestType == SIOCGA80211) {
            auto *vd = static_cast<apple80211_version_data *>(data);
            memset(vd, 0, sizeof(*vd));
            vd->version = APPLE80211_VERSION;
            snprintf(vd->string, sizeof(vd->string), "AirportMT7902 1.0.0 (%s)", mDevice->firmwareVersion());
            vd->string_len = static_cast<uint16_t>(strlen(vd->string));
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_HARDWARE_VERSION:
        if (requestType == SIOCGA80211) {
            auto *vd = static_cast<apple80211_version_data *>(data);
            memset(vd, 0, sizeof(*vd));
            vd->version = APPLE80211_VERSION;
            snprintf(vd->string, sizeof(vd->string), "MediaTek MT7902 rev 0x%08x", mDevice->chipRev());
            vd->string_len = static_cast<uint16_t>(strlen(vd->string));
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_LINK_CHANGED_EVENT_DATA:
        if (requestType == SIOCGA80211) {
            auto *lc = static_cast<apple80211_link_changed_event_data *>(data);
            memset(lc, 0, sizeof(*lc));
            lc->isLinkDown = (lk.state < mt7902::kStaRun);
            lc->rssi = lk.rssi;
            lc->voluntary = true;
            lc->reason = lk.lastDeauthReason;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_POWERSAVE:
        if (requestType == SIOCGA80211) {
            auto *ps = static_cast<apple80211_powersave_data *>(data);
            ps->version = APPLE80211_VERSION;
            ps->powersave_level = APPLE80211_POWERSAVE_MODE_DISABLED;
            return kIOReturnSuccess;
        }
        break;

    case APPLE80211_IOC_TX_ANTENNA:
    case APPLE80211_IOC_ANTENNA_DIVERSITY:
        if (requestType == SIOCGA80211) {
            auto *ad = static_cast<apple80211_antenna_data *>(data);
            ad->version = APPLE80211_VERSION;
            ad->num_radios = 1;
            ad->antenna_index[0] = 1;
            return kIOReturnSuccess;
        }
        break;

    default:
        return kIOReturnUnsupported;
    }

    return kIOReturnUnsupported;
}

#endif // KERNEL && !IO80211FAMILY_V2
