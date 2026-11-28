// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#if defined(KERNEL) && defined(IO80211FAMILY_V2)

#include "AirportMT7902ControllerV2.hpp"
#include <net/if.h>
#include <sys/kpi_mbuf.h>

#define super IO80211Controller
OSDefineMetaClassAndStructors(AirportMT7902, IO80211Controller);

IO80211WorkQueue *gMT7902WorkQueue = nullptr;
IOCommandGate *gMT7902CommandGate = nullptr;

static IOPMPowerState kV2PowerStates[2] = {
    { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 }
};

bool AirportMT7902::init(OSDictionary *properties) {
    mPowerState = 0;
    mDriverAvailableReported = false;
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
    IONetworkMedium *ieeeMed = IONetworkMedium::medium(kIOMediumIEEE80211, 600000000ULL);
    if (!ieeeMed)
        return false;
    IONetworkMedium::addMedium(mMediumDict, ieeeMed);
    if (primary) *primary = ieeeMed;
    ieeeMed->release();

    if (IONetworkMedium *noneMed = IONetworkMedium::medium(kIOMediumIEEE80211None, 0)) {
        IONetworkMedium::addMedium(mMediumDict, noneMed);
        noneMed->release();
    }
    return publishMediumDictionary(mMediumDict);
}

bool AirportMT7902::start(IOService *provider) {
    UInt8 builtIn = 0;
    if (OSData *biData = OSData::withBytes(&builtIn, sizeof(builtIn))) {
        setProperty("built-in", biData);
        biData->release();
    }
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

    mCommandGate = IOCommandGate::commandGate(this);
    if (!mCommandGate || gMT7902WorkQueue->addEventSource(mCommandGate) != kIOReturnSuccess) {
        releaseResources();
        return false;
    }
    mCommandGate->enable();
    gMT7902CommandGate = mCommandGate;

    // Allocate & enable PCIe MSI interrupt BEFORE probeAndInit() (matching Linux mt7921_pci_probe)
    // so PCIe config has MSI=Enable+ and DisINTx+ before WFSYS reset and WFDMA0 init.
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

    mWatchdogTimer = IOTimerEventSource::timerEventSource(this, &AirportMT7902::watchdogFired);
    if (mWatchdogTimer && gMT7902WorkQueue->addEventSource(mWatchdogTimer) == kIOReturnSuccess)
        mWatchdogTimer->setTimeoutMS(1000);

    mReorderTimer = IOTimerEventSource::timerEventSource(this, &AirportMT7902::reorderTimerFired);
    if (mReorderTimer && gMT7902WorkQueue->addEventSource(mReorderTimer) != kIOReturnSuccess)
        OSSafeReleaseNULL(mReorderTimer);

    const IONetworkMedium *primary = nullptr;
    if (!createMediumTables(&primary) || !setCurrentMedium(primary) || !setSelectedMedium(primary)) {
        releaseResources();
        return false;
    }

    mProtoIf = new AirportMT7902SkywalkInterface();
    if (!mProtoIf || !mProtoIf->initWithController(this, mSta)) {
        releaseResources();
        return false;
    }
    mProtoIf->setInterfaceRole(1);
    mProtoIf->setInterfaceId(1);

    if (!mProtoIf->attach(this) || !attachInterface(mProtoIf, this)) {
        releaseResources();
        return false;
    }

#ifdef AIRPORT_WCL
    mSkywalkNetIf = mProtoIf;
    const bool immediateBsdAttach = false;
#else
    mSkywalkNetIf = mProtoIf;
    const bool immediateBsdAttach = true;
#endif

    if (!IONetworkController::attachInterface(
            reinterpret_cast<IONetworkInterface **>(&mBsdIf), immediateBsdAttach)) {
        releaseResources();
        return false;
    }

    IOSkywalkEthernetInterface::RegistrationInfo registInfo = {};
    if (!mProtoIf->initRegistrationInfo(&registInfo, 1, sizeof(registInfo))) {
        releaseResources();
        return false;
    }

    mProtoIf->mExpansionData->fRegistrationInfo =
        static_cast<IOSkywalkNetworkInterface::RegistrationInfo *>(
            IOMalloc(sizeof(IOSkywalkNetworkInterface::RegistrationInfo)));
    mProtoIf->mExpansionData2->fRegistrationInfo =
        static_cast<IOSkywalkEthernetInterface::RegistrationInfo *>(
            IOMalloc(sizeof(IOSkywalkEthernetInterface::RegistrationInfo)));
    if (!mProtoIf->mExpansionData->fRegistrationInfo ||
        !mProtoIf->mExpansionData2->fRegistrationInfo) {
        releaseResources();
        return false;
    }
    memcpy(mProtoIf->mExpansionData->fRegistrationInfo, &registInfo, sizeof(registInfo));
    memcpy(mProtoIf->mExpansionData2->fRegistrationInfo, &registInfo, sizeof(registInfo));
    if (mProtoIf->getInterfaceRole() == 1)
        mProtoIf->deferBSDAttach(true);
    if (!mProtoIf->start(this)) {
        releaseResources();
        return false;
    }

    setLinkStatus(kIONetworkLinkValid);

#ifdef AIRPORT_WCL
    if (mBsdIf)
        mBsdIf->registerService();
#endif

    registerService();
    MTK_LOG("AirportMT7902 (V2/Skywalk) started successfully");
    return true;
}

void AirportMT7902::stop(IOService *provider) {
#ifdef AIRPORT_WCL
    if (mProtoIf)
        mProtoIf->notifyDriverAvailable(false);
#endif
    if (mBsdIf) {
        IONetworkController::detachInterface(mBsdIf, true);
        OSSafeReleaseNULL(mBsdIf);
    }
    if (mProtoIf) {
        detachInterface(mProtoIf, true);
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
        if (mReorderTimer) {
            mReorderTimer->cancelTimeout();
            gMT7902WorkQueue->removeEventSource(mReorderTimer);
            OSSafeReleaseNULL(mReorderTimer);
            mReorderTimerArmed = false;
        }
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
            if (gMT7902CommandGate == mCommandGate)
                gMT7902CommandGate = nullptr;
            gMT7902WorkQueue->removeEventSource(mCommandGate);
            OSSafeReleaseNULL(mCommandGate);
        }
    }
    if (mSta) {
        mSta->setListener(nullptr);
        (void)mSta->disassociate(8);
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

IOReturn AirportMT7902::enable(IONetworkInterface *netif) {
    (void)netif;
    if (!mSta)
        return kIOReturnNotReady;
    if (mPowerState && !mSta->isPoweredOn()) {
        if (mSta->setPower(true) != mt7902::kOk)
            return kIOReturnError;
    }
#ifdef AIRPORT_WCL
    if (mProtoIf && mSta->isPoweredOn()) {
        mProtoIf->notifyDriverAvailable(true);
        mDriverAvailableReported = true;
    }
#endif
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::disable(IONetworkInterface *netif) {
    (void)netif;
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::enable(IO80211SkywalkInterface *netif) {
#ifndef AIRPORT_WCL
    super::enable(netif);
#else
    (void)netif;
#endif
    if (mCommandGate)
        mCommandGate->enable();
    return enable(static_cast<IONetworkInterface *>(mBsdIf));
}

IOReturn AirportMT7902::disable(IO80211SkywalkInterface *netif) {
#ifndef AIRPORT_WCL
    super::disable(netif);
#else
    (void)netif;
#endif
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}

IONetworkInterface *AirportMT7902::createInterface() {
    AirportMT7902EthernetInterface *iface = new AirportMT7902EthernetInterface();
    if (!iface) return nullptr;
    if (!iface->initWithSkywalkInterfaceAndProvider(this, mProtoIf)) {
        iface->release();
        return nullptr;
    }
    return iface;
}

bool AirportMT7902::configureInterface(IONetworkInterface *netif) {
    if (!super::configureInterface(netif))
        return false;
    IONetworkData *data = netif->getParameter(kIONetworkStatsKey);
    if (data)
        mNetStats = static_cast<IONetworkStats *>(const_cast<void *>(data->getBuffer()));
#ifdef __PRIVATE_SPI__
#ifdef AIRPORT_WCL
    if (netif->configureOutputPullModel(
            1024, kIONetworkWorkLoopSynchronous, 0,
            IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0) != kIOReturnSuccess)
        return false;
#else
    netif->configureOutputPullModel(
        1024, 0, 0, IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0);
#endif
#endif
    return true;
}

UInt32 AirportMT7902::sendLinearPacket(mbuf_t m, bool kick) {
    if (!m || !mSta) {
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }
    if (mbuf_type(m) == MBUF_TYPE_FREE || !(mbuf_flags(m) & MBUF_PKTHDR)) {
        if (mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }
    size_t pktLen = mbuf_pkthdr_len(m);
    if (pktLen < mt7902::kEthHdrLen || pktLen > 2048) {
        freePacket(m);
        return kIOReturnOutputDropped;
    }

    int txErr;
    if (!mbuf_next(m) && mbuf_len(m) >= pktLen) {
        txErr = mSta->outputEthernetPacket(static_cast<const uint8_t *>(mbuf_data(m)), pktLen, kick);
        freePacket(m);
    } else {
        if (mbuf_copydata(m, 0, pktLen, mTxLinear) != 0) {
            freePacket(m);
            return kIOReturnOutputDropped;
        }
        freePacket(m);
        txErr = mSta->outputEthernetPacket(mTxLinear, pktLen, kick);
    }

    if (txErr != mt7902::kOk) {
        if (mNetStats) mNetStats->outputErrors++;
        return kIOReturnOutputDropped;
    }
    if (mNetStats) mNetStats->outputPackets++;
    return kIOReturnOutputSuccess;
}

IOReturn AirportMT7902::outputPacketGated(OSObject *owner, void *arg0, void *, void *, void *) {
    auto *self = static_cast<AirportMT7902 *>(owner);
    return self->sendLinearPacket(static_cast<mbuf_t>(arg0), true);
}

UInt32 AirportMT7902::outputPacket(mbuf_t m, void *param) {
    (void)param;
#ifdef AIRPORT_WCL
    if (gMT7902WorkQueue && !gMT7902WorkQueue->inGate() && mCommandGate) {
        IOReturn st = mCommandGate->runAction(outputPacketGated, m);
        if (st != kIOReturnOutputSuccess && st != kIOReturnOutputDropped) {
            if (m && mbuf_type(m) != MBUF_TYPE_FREE)
                freePacket(m);
            return kIOReturnOutputDropped;
        }
        return st;
    }
#endif
    return sendLinearPacket(m, true);
}

#ifdef __PRIVATE_SPI__
IOReturn AirportMT7902::outputStart(IONetworkInterface *interface, IOOptionBits options) {
    (void)options;
    if (!interface || !mDevice || !mSta)
        return kIOReturnBadArgument;
    if (mInOutputStart)
        return kIOReturnSuccess;
    mInOutputStart = true;

    for (int batch = 0; batch < 16; ++batch) {
        if (mDevice->dma().txFreeCount(mt7902::kTxBand0) < 64 ||
            mDevice->mac().freeTokens() < 64) {
            mDevice->reclaimTxResources();
            if (mDevice->dma().txFreeCount(mt7902::kTxBand0) < 32 ||
                mDevice->mac().freeTokens() < 32) {
                // Leave remaining packets safely in XNU pull queue until TX_FREE frees tokens
                mTxThrottled = true;
                break;
            }
        }
        mTxThrottled = false;

        mbuf_t m = nullptr;
        if (interface->dequeueOutputPackets(32, &m) != kIOReturnSuccess || !m)
            break;
        bool submittedAny = false;
        while (m) {
            mbuf_t next = mbuf_nextpkt(m);
            mbuf_setnextpkt(m, nullptr);
            if (sendLinearPacket(m, false) == kIOReturnOutputSuccess)
                submittedAny = true;
            m = next;
        }
        if (submittedAny)
            mSta->flushTx();
    }

    mInOutputStart = false;
    return kIOReturnSuccess;
}
#endif

IOReturn AirportMT7902::getHardwareAddress(IOEthernetAddress *addrP) {
    if (!addrP || !mDevice) return kIOReturnBadArgument;
    memcpy(addrP->bytes, mDevice->capability().macAddr, 6);
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::setHardwareAddress(const void *addrP, UInt32 addrBytes) {
    if (!addrP || addrBytes != kIOEthernetAddressSize || !mDevice)
        return kIOReturnBadArgument;
    const uint8_t *addr = static_cast<const uint8_t *>(addrP);
    auto action = mt7902::wcl::planMacUpdate(mDevice->capability().macAddr, addr, addrBytes, false);
    if (action == mt7902::wcl::MacUpdate::Invalid)
        return kIOReturnBadArgument;
    if (mBsdIf && mBsdIf->getIfnet()) {
        if (ifnet_set_lladdr(mBsdIf->getIfnet(), addr, addrBytes) != 0)
            return kIOReturnError;
        mBsdIf->setProperty(kIOMACAddress, const_cast<uint8_t *>(addr), addrBytes);
    }
    if (action == mt7902::wcl::MacUpdate::Apply) {
        MTK_LOG("Updating MT7902 MAC address to %02x:%02x:%02x:%02x:%02x:%02x",
                addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
        if (mSta)
            (void)mSta->setMacAddress(addr);
        else
            (void)mDevice->setMacAddress(addr);
    }
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
#ifdef AIRPORT_WCL
    cap->capabilities[0] = (1U << APPLE80211_CAP_AES_CCM) | (1U << APPLE80211_CAP_TKIP);
    cap->capabilities[1] = (1U << (APPLE80211_CAP_SHSLOT - 8)) |
                           (1U << (APPLE80211_CAP_SHPREAMBLE - 8)) |
                           (1U << (APPLE80211_CAP_WPA2 - 8)) |
                           (1U << (APPLE80211_CAP_TKIPMIC - 8));
    cap->capabilities[2] = (1U << (APPLE80211_CAP_WME - 16)) |
                           (1U << (APPLE80211_CAP_SHORT_GI_40MHZ - 16)) |
                           (1U << (APPLE80211_CAP_SHORT_GI_20MHZ - 16));
    cap->capabilities[5] = 0x40; // Scan completion events required by WCL
    if (mDevice && mDevice->capability().supportsHe)
        cap->capabilities[17] |= 0x80; // 802.11ax HE capability bit for WCL
#else
    uint32_t caps = (1U << APPLE80211_CAP_WEP) | (1U << APPLE80211_CAP_TKIP) |
                    (1U << APPLE80211_CAP_AES) | (1U << APPLE80211_CAP_AES_CCM) |
                    (1U << APPLE80211_CAP_WPA) | (1U << APPLE80211_CAP_WPA1) |
                    (1U << APPLE80211_CAP_WPA2) | (1U << APPLE80211_CAP_WME) |
                    (1U << APPLE80211_CAP_SHORT_GI_20MHZ) | (1U << APPLE80211_CAP_SHORT_GI_40MHZ);
    cap->capabilities[0] = static_cast<uint8_t>(caps & 0xffU);
    cap->capabilities[1] = static_cast<uint8_t>((caps >> 8) & 0xffU);
    cap->capabilities[2] = 0xffU;
    cap->capabilities[3] = 0x2bU;
    cap->capabilities[5] = 0x40U;
    cap->capabilities[6] = 0x8cU;
    *reinterpret_cast<uint16_t *>(&cap->capabilities[8]) = 0x0201U;
#endif
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::getPOWER(IO80211SkywalkInterface *, apple80211_power_data *pd) {
    if (!pd || !mSta) return kIOReturnBadArgument;
    memset(pd, 0, sizeof(*pd));
    pd->version = APPLE80211_VERSION;
    pd->num_radios = 4;
    uint32_t state = mSta->isPoweredOn() ? APPLE80211_POWER_ON : APPLE80211_POWER_OFF;
    pd->power_state[0] = state;
    pd->power_state[1] = state;
    pd->power_state[2] = state;
    pd->power_state[3] = state;
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::setPOWER(IO80211SkywalkInterface *, apple80211_power_data *pd) {
    if (!pd || !mSta) return kIOReturnBadArgument;
    bool on = (pd->num_radios > 0 && pd->power_state[0] != APPLE80211_POWER_OFF);
    MTK_LOG("setPOWER: num_radios=%u state=%u -> on=%d", pd->num_radios, pd->power_state[0], on);
    bool wasOn = mSta->isPoweredOn();
    int err = mSta->setPower(on);
    if (err != mt7902::kOk)
        return kIOReturnError;
    mPowerState = on ? 1 : 0;
#ifdef AIRPORT_WCL
    if (mProtoIf) {
        if (on && (!wasOn || !mDriverAvailableReported)) {
            mProtoIf->notifyDriverAvailable(true);
            mDriverAvailableReported = true;
        } else if (!on) {
            mProtoIf->notifyDriverAvailable(false);
            mDriverAvailableReported = false;
        }
    }
#endif
    return kIOReturnSuccess;
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
    if (cc->cc[0] != 'x' && cc->cc[0] != 'X' && cc->cc[0] != 0)
        (void)mSta->setCountryCode(reinterpret_cast<const char *>(cc->cc));
    return kIOReturnSuccess;
}

IOReturn AirportMT7902::registerWithPolicyMaker(IOService *policyMaker) {
    return policyMaker->registerPowerDriver(this, kV2PowerStates, 2);
}

IOReturn AirportMT7902::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker) {
    (void)policyMaker;
    if (mDevice) {
        if (powerStateOrdinal == 0) {
            if (mSta) (void)mSta->disassociate(8);
            (void)mDevice->suspend();
        } else {
            (void)mDevice->resume();
        }
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
    if (self->mSta) {
        self->onRxBatchBegin();
        self->mSta->pollPeriodic();
        self->onRxBatchComplete();
    }
    timer->setTimeoutMS(1000);
}

void AirportMT7902::reorderTimerFired(OSObject *owner, IOTimerEventSource *) {
    AirportMT7902 *self = OSDynamicCast(AirportMT7902, owner);
    if (!self) return;
    self->mReorderTimerArmed = false;
    if (self->mSta) {
        self->onRxBatchBegin();
        self->mSta->pollRxReorderTimeouts();
        self->onRxBatchComplete();
    }
}

void AirportMT7902::onRxBatchBegin() {
    mInRxBatch = true;
}

void AirportMT7902::onRxBatchComplete() {
    mInRxBatch = false;
    if (mBsdIf && mPendingInputPackets > 0) {
        mBsdIf->flushInputQueue();
        mPendingInputPackets = 0;
    }
    if (mReorderTimer && mSta && mSta->hasPendingReorderFrames() && !mReorderTimerArmed) {
        mReorderTimer->setTimeoutMS(40);
        mReorderTimerArmed = true;
    }
#ifdef __PRIVATE_SPI__
    if (mTxThrottled && !mInOutputStart && mBsdIf && mDevice &&
        mDevice->dma().txFreeCount(mt7902::kTxBand0) >= 64 &&
        mDevice->mac().freeTokens() >= 64) {
        mTxThrottled = false;
        outputStart(mBsdIf, 0);
    }
#endif
}

void AirportMT7902::onDataFrameReceived(const uint8_t *ethFrame, size_t len) {
    if (!mBsdIf || !ethFrame || len < mt7902::kEthHdrLen)
        return;
    mbuf_t m = allocatePacket(static_cast<UInt32>(len));
    if (!m) return;
    if (!mbuf_next(m) && mbuf_len(m) >= len) {
        memcpy(mbuf_data(m), ethFrame, len);
        mbuf_setlen(m, len);
        mbuf_pkthdr_setlen(m, len);
    } else if (mbuf_copyback(m, 0, len, ethFrame, MBUF_DONTWAIT) != 0) {
        freePacket(m);
        return;
    }
    IOOptionBits opts = mInRxBatch ? IONetworkInterface::kInputOptionQueuePacket : 0;
    mBsdIf->inputPacket(m, static_cast<UInt32>(len), opts);
    if (mInRxBatch)
        mPendingInputPackets++;
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
        const auto &lk = mSta->link();
        uint32_t minMbps = (lk.band != mt7902::kBand2GHz && lk.bw >= mt7902::CMD_CBW_80MHZ)
                               ? 433U
                               : (lk.bw >= mt7902::CMD_CBW_40MHZ ? 200U : 144U);
        uint32_t reportMbps = (lk.txRateMbps > minMbps) ? lk.txRateMbps : minMbps;
        UInt64 speed = static_cast<UInt64>(reportMbps) * 1000000ULL;
        setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, getCurrentMedium(), speed);
#ifdef __PRIVATE_SPI__
        if (mBsdIf)
            mBsdIf->startOutputThread();
#endif
        if (mBsdIf)
            mBsdIf->setLinkState(kIO80211NetworkLinkUp);
    } else {
        mTxThrottled = false;
        if (mReorderTimer && mReorderTimerArmed) {
            mReorderTimer->cancelTimeout();
            mReorderTimerArmed = false;
        }
        setLinkStatus(kIONetworkLinkValid, getCurrentMedium(), 0);
#ifdef __PRIVATE_SPI__
        if (mBsdIf) {
            mBsdIf->stopOutputThread();
            mBsdIf->flushOutputQueue();
        }
#endif
        if (mBsdIf)
            mBsdIf->setLinkState(kIO80211NetworkLinkDown);
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
