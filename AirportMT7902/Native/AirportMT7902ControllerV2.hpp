// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Native IO80211ControllerV2 for macOS 14 Sonoma, 15 Sequoia, and 26 Tahoe

#ifndef AIRPORT_MT7902_CONTROLLER_V2_HPP
#define AIRPORT_MT7902_CONTROLLER_V2_HPP

#if defined(KERNEL) && defined(IO80211FAMILY_V2)

#include <Airport/Apple80211.h>
#ifdef AIRPORT_WCL
#if __IO80211_TARGET == __MAC_15_2
#include <Airport/macOS15/FaultReporter.h>
#else
#include <Airport/macOS26/FaultReporter.h>
#endif
#endif

#include <IOKit/IOCommandGate.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#include <IOKit/pci/IOPCIDevice.h>

#include "AirportMT7902EthernetInterface.hpp"
#include "AirportMT7902SkywalkInterface.hpp"
#include "MT7902IOKitBus.hpp"
#include "MT7902PCIWrapper.hpp"
#include "../HAL/MT7902Device.hpp"
#include "../Net80211/MT7902Sta.hpp"

#ifdef AIRPORT_WCL
#define MTK_V2_LEGACY_OVERRIDE
#else
#define MTK_V2_LEGACY_OVERRIDE override
#endif

class AirportMT7902 : public IO80211Controller, public mt7902::IStaEventListener {
    OSDeclareDefaultStructors(AirportMT7902)

public:
    bool init(OSDictionary *properties) override;
    void free() override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

    bool createWorkQueue() override;
#ifdef AIRPORT_WCL
    IOWorkLoop *getWorkLoop() const override;
    IO80211WorkQueue *getWorkQueue() const override;
    CCLogStream *getLogger() const override { return mDriverLogger; }
    CommonFaultReporter *getFaultReporterFromDriver() override { return mFaultReporter; }
#else
    IO80211WorkQueue *getWorkQueue() override;
    void *getFaultReporterFromDriver() override { return mFaultStream; }
#endif

    IOReturn enable(IONetworkInterface *netif) override;
    IOReturn disable(IONetworkInterface *netif) override;
    IOReturn enable(IO80211SkywalkInterface *netif) MTK_V2_LEGACY_OVERRIDE;
    IOReturn disable(IO80211SkywalkInterface *netif) MTK_V2_LEGACY_OVERRIDE;
    IONetworkInterface *createInterface() override;
    bool configureInterface(IONetworkInterface *netif) override;
    UInt32 outputPacket(mbuf_t m, void *param) override;
#ifdef __PRIVATE_SPI__
    IOReturn outputStart(IONetworkInterface *interface, IOOptionBits options) override;
#endif

    IOReturn getHardwareAddress(IOEthernetAddress *addrP) override;
    IOReturn setHardwareAddress(const void *addrP, UInt32 addrBytes) override;
    IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const override;
    IOReturn setMulticastMode(bool active) override;
    IOReturn setPromiscuousMode(bool active) override;
    UInt32 getFeatures() const override;
    const OSString *newVendorString() const override;
    const OSString *newModelString() const override;
    IOReturn selectMedium(const IONetworkMedium *medium) override;

    bool requiresExplicitMBufRelease() override { return false; }
    bool flowIdSupported() override { return false; }
    SInt32 monitorModeSetEnabled(bool, unsigned int) override { return kIOReturnSuccess; }
    bool getLogPipes(CCPipe **logPipe, CCPipe **dataPipe, CCPipe **snapPipe) override;

    // Required pure-virtuals on IO80211ControllerV2
    bool isCommandProhibited(int command) override { (void)command; return false; }
    SInt32 handleCardSpecific(IO80211SkywalkInterface *, unsigned long, void *, bool) override { return 0; }
    SInt32 enableFeature(IO80211FeatureCode, void *) override { return kIOReturnSuccess; }
    IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *, apple80211_version_data *data) override;
    IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *, apple80211_version_data *data) override;
    IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *, apple80211_capability_data *data) override;
    IOReturn getPOWER(IO80211SkywalkInterface *, apple80211_power_data *data) override;
    IOReturn setPOWER(IO80211SkywalkInterface *, apple80211_power_data *data) override;
    IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *data) override;
    IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *data) override;
    IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *, apple80211_debug_command *) override { return kIOReturnSuccess; }

    // Power management
    IOReturn registerWithPolicyMaker(IOService *policyMaker) override;
    IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker) override;

    // IStaEventListener callbacks
    void onDataFrameReceived(const uint8_t *ethFrame, size_t len) override;
    void onScanFinished() override;
    void onBssDiscovered(const mt7902::ScannedBss &bss) override;
    void onAssociationCompleted(bool success, uint16_t statusCode) override;
    void onLinkStateChanged(bool up) override;
    void onDeauthenticated(uint16_t reasonCode, bool isDisassoc) override;
    void onAssociatedBeacon(const mt7902::ScannedBss &bss) override;

    AirportMT7902EthernetInterface *bsdInterface() const { return mBsdIf; }

private:
    static void interruptOccurred(OSObject *owner, IOInterruptEventSource *src, int count);
    static void watchdogFired(OSObject *owner, IOTimerEventSource *timer);
    static IOReturn outputPacketGated(OSObject *owner, void *arg0, void *, void *, void *);
    UInt32 sendLinearPacket(mbuf_t m);
    bool initCoreCaptureLogs();
    bool createMediumTables(const IONetworkMedium **primary);
    void releaseResources();

    IOPCIDevice *mPciNub = nullptr;
    IOCommandGate *mCommandGate = nullptr;
    IOInterruptEventSource *mInterruptSrc = nullptr;
    IOTimerEventSource *mWatchdogTimer = nullptr;
    AirportMT7902EthernetInterface *mBsdIf = nullptr;
    AirportMT7902SkywalkInterface *mProtoIf = nullptr;
#ifdef AIRPORT_WCL
    IO80211InfraInterface *mSkywalkNetIf = nullptr;
#else
    IO80211SkywalkInterface *mSkywalkNetIf = nullptr;
#endif
    IONetworkStats *mNetStats = nullptr;
    OSDictionary *mMediumDict = nullptr;

    mt7902::MT7902IOKitBus *mBus = nullptr;
    mt7902::MT7902Device *mDevice = nullptr;
    mt7902::MT7902Sta *mSta = nullptr;
    uint32_t mPowerState = 1;
    bool mDriverAvailableReported = false;

    // CoreCapture & FaultReporter objects required by Sonoma/Sequoia/Tahoe IO80211Family
    CCPipe *mLogPipe = nullptr;
    CCPipe *mDataPipe = nullptr;
    CCPipe *mSnapPipe = nullptr;
    CCStream *mFaultStream = nullptr;
#ifdef AIRPORT_WCL
    bool mLogPipeStarted = false;
    bool mDataPipeStarted = false;
    bool mSnapPipeStarted = false;
    CCLogStream *mDriverLogger = nullptr;
    CCFaultReporter *mCoreFaultReporter = nullptr;
    IO80211FaultReporter *mFaultReporter = nullptr;
#endif
};

#endif // KERNEL && IO80211FAMILY_V2
#endif // AIRPORT_MT7902_CONTROLLER_V2_HPP
