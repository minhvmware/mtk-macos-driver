// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#if defined(KERNEL) && defined(IO80211FAMILY_V2)

#include "AirportMT7902ControllerV2.hpp"
#include <sys/kpi_mbuf.h>

#define super IO80211Controller
OSDefineMetaClassAndStructors(AirportMT7902, IO80211Controller);

IO80211WorkQueue *gMT7902WorkQueue = nullptr;

static IOPMPowerState kV2PowerStates[2] = {
    { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 }
};

bool AirportMT7902::init(OSDictionary *properties) {
    return super::init(properties);
}

void AirportMT7902::free() {
    releaseResources();
    super::free();
}

IOService *AirportMT7902::probe(IOService *provider, SInt32 *score) {
    MT7902PCIWrapper *wrapper = OSDynamicCast(MT7902PCIWrapper, provider);
    if (!wrapper || !wrapper->pciNub)
        return nullptr;
    mPciNub = wrapper->pciNub;
    return super::probe(provider, score);
}

bool AirportMT7902::createWorkQueue() {
    if (!gMT7902WorkQueue)
        gMT7902WorkQueue = IO80211WorkQueue::workQueue();
    return gMT7902WorkQueue != nullptr;
}

#ifdef AIRPORT_WCL
IOWorkLoop *AirportMT7902::getWorkLoop() const {
    return gMT7902WorkQueue;
}
IO80211WorkQueue *AirportMT7902::getWorkQueue() const {
    return gMT7902WorkQueue;
}
#else
IO80211WorkQueue *AirportMT7902::getWorkQueue() {
    return gMT7902WorkQueue;
}
#endif

bool AirportMT7902::initCoreCaptureLogs() {
    CCPipeOptions opts = {};
    opts.pipe_type = 0;
    opts.log_data_type = 1;
    opts.pipe_size = 0x200000;
    opts.min_log_size_notify = 0xccccc;
    opts.notify_threshold = 1000;
    strlcpy(opts.file_name, "MT7902_Logs", sizeof(opts.file_name));
    strlcpy(opts.name, "wlan0", sizeof(opts.name));
    strlcpy(opts.directory_name, "WiFi", sizeof(opts.directory_name));
    opts.pad9 = 0x1000000;
    opts.pad10 = 2;
    mLogPipe = CCPipe::withOwnerNameCapacity(this, "com.mtk.AirportMT7902", "DriverLogs", &opts);
    if (!mLogPipe)
        return false;

#ifdef AIRPORT_WCL
    if (!getWorkLoop())
        return false;
    mLogPipeStarted = mLogPipe->CCPipe::startPipe();
    if (!mLogPipeStarted)
        return false;
    CCStreamOptions logStreamOpts = {};
    CCStream *ls = CCStream::withPipeAndName(mLogPipe, "AirportMT7902", &logStreamOpts);
    mDriverLogger = OSDynamicCast(CCLogStream, ls);
    if (!mDriverLogger) {
        OSSafeReleaseNULL(ls);
        return false;
    }
#endif

    memset(&opts, 0, sizeof(opts));
    opts.pipe_size = 0x200000;
    opts.min_log_size_notify = 0xccccc;
    opts.notify_threshold = 1000;
    strlcpy(opts.file_name, "MT7902_Datapath", sizeof(opts.file_name));
    strlcpy(opts.directory_name, "WiFi", sizeof(opts.directory_name));
    mDataPipe = CCPipe::withOwnerNameCapacity(this, "com.mtk.AirportMT7902", "DatapathEvents", &opts);
    if (!mDataPipe)
        return false;
#ifdef AIRPORT_WCL
    mDataPipeStarted = mDataPipe->CCPipe::startPipe();
    if (!mDataPipeStarted)
        return false;
#endif

    memset(&opts, 0, sizeof(opts));
    opts.pipe_type = 0x200000001ULL;
    opts.log_data_type = 2;
    opts.pipe_size = 128;
    strlcpy(opts.file_name, "StateSnapshots", sizeof(opts.file_name));
    strlcpy(opts.name, "0", sizeof(opts.name));
    strlcpy(opts.directory_name, "WiFi", sizeof(opts.directory_name));
    mSnapPipe = CCPipe::withOwnerNameCapacity(this, "com.mtk.AirportMT7902", "StateSnapshots", &opts);
    if (!mSnapPipe)
        return false;
#ifdef AIRPORT_WCL
    mSnapPipeStarted = mSnapPipe->CCPipe::startPipe();
    if (!mSnapPipeStarted)
        return false;
#endif

    CCStreamOptions faultOpts = {};
    faultOpts.stream_type = 1;
    faultOpts.console_level = 0xFFFFFFFFFFFFFFFFULL;
    mFaultStream = CCStream::withPipeAndName(mSnapPipe, "FaultReporter", &faultOpts);
    if (!mFaultStream)
        return false;

#ifdef AIRPORT_WCL
    CCDataStream *ds = OSDynamicCast(CCDataStream, mFaultStream);
    if (!ds || !gMT7902WorkQueue)
        return false;
    mCoreFaultReporter = CCFaultReporter::withStreamWorkloop(ds, gMT7902WorkQueue);
    if (!mCoreFaultReporter)
        return false;
    mFaultReporter = IO80211FaultReporter::allocWithParams(mCoreFaultReporter);
    if (!OSDynamicCast(CommonFaultReporter, mFaultReporter))
        return false;
#endif
    return true;
}

bool AirportMT7902::getLogPipes(CCPipe **logPipe, CCPipe **dataPipe, CCPipe **snapPipe) {
    if (logPipe) *logPipe = mLogPipe;
    if (dataPipe) *dataPipe = mDataPipe;
    if (snapPipe) *snapPipe = mSnapPipe;
    return mLogPipe && mDataPipe && mSnapPipe;
}

bool AirportMT7902::createMediumTables(const IONetworkMedium **primary) {
    mMediumDict = OSDictionary::withCapacity(2);
    if (!mMediumDict)
        return false;
    IONetworkMedium *autoMed = IONetworkMedium::medium(kIOMediumIEEE80211Auto, 1200000000ULL);
    if (!autoMed)
        return false;
    IONetworkMedium::addMedium(mMediumDict, autoMed);
    if (primary) *primary = autoMed;
    autoMed->release();
    return publishMediumDictionary(mMediumDict);
}

bool AirportMT7902::start(IOService *provider) {
    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    setProperty("DriverKitDriver", kOSBooleanFalse);

    createWorkQueue();
    if (!initCoreCaptureLogs()) {
        MTK_ERR("Failed to initialize CoreCapture pipes");
        releaseResources();
        return false;
    }

    if (!super::start(provider)) {
        releaseResources();
        return false;
    }

    mBus = new mt7902::MT7902IOKitBus();
    if (!mBus || !mBus->attach(mPciNub)) {
        releaseResources();
        return false;
    }

    mDevice = new mt7902::MT7902Device(*mBus);
    if (!mDevice || mDevice->probeAndInit() != mt7902::kOk) {
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
    if (!mCommandGate || gMT7902WorkQueue->addEventSource(mCommandGate) != kIOReturnSuccess) {
        releaseResources();
        return false;
    }

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
    if (!mInterruptSrc || gMT7902WorkQueue->addEventSource(mInterruptSrc) != kIOReturnSuccess) {
        releaseResources();
        return false;
    }
    mInterruptSrc->enable();

    mWatchdogTimer = IOTimerEventSource::timerEventSource(this, &AirportMT7902::watchdogFired);
    if (mWatchdogTimer && gMT7902WorkQueue->addEventSource(mWatchdogTimer) == kIOReturnSuccess)
        mWatchdogTimer->setTimeoutMS(1000);

    const IONetworkMedium *primary = nullptr;
    if (!createMediumTables(&primary) || !setCurrentMedium(primary)) {
        releaseResources();
        return false;
    }

    mProtoIf = new AirportMT7902SkywalkInterface();
    if (!mProtoIf || !mProtoIf->initWithController(this, mSta)) {
        releaseResources();
        return false;
    }

#ifdef AIRPORT_WCL
    mSkywalkNetIf = OSDynamicCast(IO80211InfraInterface, getPrimarySkywalkInterface());
#else
    mSkywalkNetIf = getPrimarySkywalkInterface();
#endif

    if (mSkywalkNetIf) {
        mBsdIf = new AirportMT7902EthernetInterface();
        if (mBsdIf && mBsdIf->initWithSkywalkInterfaceAndProvider(this, mSkywalkNetIf)) {
            attachInterface(reinterpret_cast<IONetworkInterface **>(&mBsdIf), true);
        }
    }

    registerService();
    MTK_LOG("AirportMT7902 (V2/Skywalk) started successfully");
    return true;
}

void AirportMT7902::stop(IOService *provider) {
    if (mBsdIf) {
        detachInterface(mBsdIf, true);
        OSSafeReleaseNULL(mBsdIf);
    }
    releaseResources();
    super::stop(provider);
}

void AirportMT7902::releaseResources() {
#ifdef AIRPORT_WCL
    OSSafeReleaseNULL(mFaultReporter);
    OSSafeReleaseNULL(mCoreFaultReporter);
    OSSafeReleaseNULL(mDriverLogger);
    if (mSnapPipeStarted && mSnapPipe) { mSnapPipe->CCPipe::stopPipe(); mSnapPipeStarted = false; }
    if (mDataPipeStarted && mDataPipe) { mDataPipe->CCPipe::stopPipe(); mDataPipeStarted = false; }
    if (mLogPipeStarted && mLogPipe)   { mLogPipe->CCPipe::stopPipe();  mLogPipeStarted = false; }
#endif
    OSSafeReleaseNULL(mFaultStream);
    OSSafeReleaseNULL(mSnapPipe);
    OSSafeReleaseNULL(mDataPipe);
    OSSafeReleaseNULL(mLogPipe);
    OSSafeReleaseNULL(mProtoIf);

    if (gMT7902WorkQueue) {
        if (mWatchdogTimer) {
            mWatchdogTimer->cancelTimeout();
            gMT7902WorkQueue->removeEventSource(mWatchdogTimer);
            OSSafeReleaseNULL(mWatchdogTimer);
        }
        if (mInterruptSrc) {
            mInterruptSrc->disable();
            gMT7902WorkQueue->removeEventSource(mInterruptSrc);
            OSSafeReleaseNULL(mInterruptSrc);
        }
        if (mCommandGate) {
            gMT7902WorkQueue->removeEventSource(mCommandGate);
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
    OSSafeReleaseNULL(mMediumDict);
}

IOReturn AirportMT7902::enable(IO80211SkywalkInterface *netif) {
    (void)netif;
    if (!mSta) return kIOReturnNotReady;
    if (mSta->setPower(true) != mt7902::kOk)
        return kIOReturnError;
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::disable(IO80211SkywalkInterface *netif) {
    (void)netif;
    if (mSta) (void)mSta->setPower(false);
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}

IONetworkInterface *AirportMT7902::createInterface() {
    AirportMT7902EthernetInterface *iface = new AirportMT7902EthernetInterface();
    if (!iface) return nullptr;
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

    if (mSta->outputEthernetPacket(linear, pktLen) != mt7902::kOk) {
        if (mNetStats) mNetStats->outputErrors++;
        return kIOReturnOutputDropped;
    }
    if (mNetStats) mNetStats->outputPackets++;
    return kIOReturnOutputSuccess;
}

IOReturn AirportMT7902::getHardwareAddress(IOEthernetAddress *addrP) {
    if (!addrP || !mDevice) return kIOReturnBadArgument;
    memcpy(addrP->bytes, mDevice->capability().macAddr, 6);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::getPacketFilters(const OSSymbol *group, UInt32 *filters) const {
    if (group == gIONetworkFilterGroup) {
        *filters = kIOPacketFilterUnicast | kIOPacketFilterBroadcast |
                   kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
        return kIOReturnSuccess;
    }
    return super::getPacketFilters(group, filters);
}

IOReturn AirportMT7902::setMulticastMode(bool active) { (void)active; return kIOReturnSuccess; }
IOReturn AirportMT7902::setPromiscuousMode(bool active) { (void)active; return kIOReturnSuccess; }
UInt32 AirportMT7902::getFeatures() const { return kIONetworkFeatureMultiPages; }
const OSString *AirportMT7902::newVendorString() const { return OSString::withCString("MediaTek"); }
const OSString *AirportMT7902::newModelString() const { return OSString::withCString("MT7902 Wi-Fi 6E (Filogic 310)"); }

IOReturn AirportMT7902::selectMedium(const IONetworkMedium *medium) {
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::getDRIVER_VERSION(IO80211SkywalkInterface *, apple80211_version_data *vd) {
    if (!vd || !mDevice) return kIOReturnBadArgument;
    memset(vd, 0, sizeof(*vd));
    vd->version = APPLE80211_VERSION;
    snprintf(vd->string, sizeof(vd->string), "AirportMT7902 1.0.0 (%s)", mDevice->firmwareVersion());
    vd->string_len = static_cast<uint16_t>(strlen(vd->string));
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::getHARDWARE_VERSION(IO80211SkywalkInterface *, apple80211_version_data *vd) {
    if (!vd || !mDevice) return kIOReturnBadArgument;
    memset(vd, 0, sizeof(*vd));
    vd->version = APPLE80211_VERSION;
    snprintf(vd->string, sizeof(vd->string), "MediaTek MT7902 rev 0x%08x", mDevice->chipRev());
    vd->string_len = static_cast<uint16_t>(strlen(vd->string));
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::getCARD_CAPABILITIES(IO80211SkywalkInterface *, apple80211_capability_data *cap) {
    if (!cap) return kIOReturnBadArgument;
    memset(cap, 0, sizeof(*cap));
    cap->version = APPLE80211_VERSION;
    uint32_t caps = (1U << APPLE80211_CAP_WEP) | (1U << APPLE80211_CAP_TKIP) |
                    (1U << APPLE80211_CAP_AES) | (1U << APPLE80211_CAP_AES_CCM) |
                    (1U << APPLE80211_CAP_WPA) | (1U << APPLE80211_CAP_WPA1) |
                    (1U << APPLE80211_CAP_WPA2) | (1U << APPLE80211_CAP_WME) |
                    (1U << APPLE80211_CAP_SHORT_GI_20MHZ) | (1U << APPLE80211_CAP_SHORT_GI_40MHZ);
    cap->capabilities[0] = static_cast<uint8_t>(caps & 0xffU);
    cap->capabilities[1] = static_cast<uint8_t>((caps >> 8) & 0xffU);
    cap->capabilities[2] = static_cast<uint8_t>((caps >> 16) & 0xffU);
    cap->capabilities[3] = static_cast<uint8_t>((caps >> 24) & 0xffU);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::getPOWER(IO80211SkywalkInterface *, apple80211_power_data *pd) {
    if (!pd || !mSta) return kIOReturnBadArgument;
    memset(pd, 0, sizeof(*pd));
    pd->version = APPLE80211_VERSION;
    pd->num_radios = 1;
    pd->power_state[0] = mSta->isPoweredOn() ? APPLE80211_POWER_ON : APPLE80211_POWER_OFF;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::setPOWER(IO80211SkywalkInterface *, apple80211_power_data *pd) {
    if (!pd || !mSta) return kIOReturnBadArgument;
    bool on = (pd->num_radios > 0 && pd->power_state[0] != APPLE80211_POWER_OFF);
    return (mSta->setPower(on) == mt7902::kOk) ? kIOReturnSuccess : kIOReturnError;
}

IOReturn AirportMT7902::getCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *cc) {
    if (!cc || !mSta) return kIOReturnBadArgument;
    memset(cc, 0, sizeof(*cc));
    cc->version = APPLE80211_VERSION;
    memcpy(cc->cc, mSta->countryCode(), 2);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::setCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *cc) {
    if (!cc || !mSta) return kIOReturnBadArgument;
    (void)mSta->setCountryCode(reinterpret_cast<const char *>(cc->cc));
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::registerWithPolicyMaker(IOService *policyMaker) {
    return policyMaker->registerPowerDriver(this, kV2PowerStates, 2);
}

IOReturn AirportMT7902::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker) {
    (void)policyMaker;
    if (mDevice) {
        if (powerStateOrdinal == 0) (void)mDevice->suspend();
        else (void)mDevice->resume();
    }
    return IOPMAckImplied;
}

void AirportMT7902::interruptOccurred(OSObject *owner, IOInterruptEventSource *, int) {
    AirportMT7902 *self = OSDynamicCast(AirportMT7902, owner);
    if (self && self->mDevice)
        self->mDevice->serviceInterrupt();
}

void AirportMT7902::watchdogFired(OSObject *owner, IOTimerEventSource *timer) {
    AirportMT7902 *self = OSDynamicCast(AirportMT7902, owner);
    if (!self) return;
    if (self->mDevice)
        self->mDevice->serviceInterrupt();
    timer->setTimeoutMS(1000);
}

void AirportMT7902::onDataFrameReceived(const uint8_t *ethFrame, size_t len) {
    if (!mBsdIf || !ethFrame || len < mt7902::kEthHdrLen)
        return;
    mbuf_t m = allocatePacket(static_cast<UInt32>(len));
    if (!m) return;
    if (mbuf_copyback(m, 0, len, ethFrame, MBUF_DONTWAIT) != 0) {
        freePacket(m);
        return;
    }
    mBsdIf->inputPacket(m, static_cast<UInt32>(len));
    if (mNetStats) mNetStats->inputPackets++;
}

void AirportMT7902::onScanFinished() {
    if (mProtoIf) mProtoIf->notifyScanDone();
}

void AirportMT7902::onBssDiscovered(const mt7902::ScannedBss &bss) {
    if (mProtoIf) mProtoIf->notifyBssDiscovered(bss);
}

void AirportMT7902::onAssociationCompleted(bool success, uint16_t statusCode) {
    if (mProtoIf) mProtoIf->notifyAssocDone(success, statusCode);
}

void AirportMT7902::onLinkStateChanged(bool up) {
    if (up) {
        UInt64 speed = static_cast<UInt64>(mSta->link().txRateMbps) * 1000000ULL;
        setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, getCurrentMedium(), speed);
    } else {
        setLinkStatus(kIONetworkLinkValid, getCurrentMedium(), 0);
    }
    if (mProtoIf) mProtoIf->notifyLinkChanged(up);
}

void AirportMT7902::onDeauthenticated(uint16_t reasonCode, bool isDisassoc) {
    if (mProtoIf) mProtoIf->notifyDeauth(reasonCode, isDisassoc);
}

void AirportMT7902::onAssociatedBeacon(const mt7902::ScannedBss &bss) {
    if (mProtoIf) mProtoIf->notifyBeacon(bss);
}

#endif // KERNEL && IO80211FAMILY_V2
