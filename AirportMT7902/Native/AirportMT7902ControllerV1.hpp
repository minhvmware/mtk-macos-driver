// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Native IO80211Controller for macOS 12 Monterey / 13 Ventura

#ifndef AIRPORT_MT7902_CONTROLLER_V1_HPP
#define AIRPORT_MT7902_CONTROLLER_V1_HPP

#if defined(KERNEL) && !defined(IO80211FAMILY_V2)

#include <Airport/Apple80211.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#include <IOKit/pci/IOPCIDevice.h>

#include "AirportMT7902Interface.hpp"
#include "MT7902IOKitBus.hpp"
#include "../HAL/MT7902Device.hpp"
#include "../Net80211/MT7902Sta.hpp"

class AirportMT7902 : public IO80211Controller, public mt7902::IStaEventListener {
    OSDeclareDefaultStructors(AirportMT7902)

public:
    // IOService / IONetworkController lifecycle
    bool init(OSDictionary *properties) override;
    void free() override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

    bool createWorkLoop() override;
    IOWorkLoop *getWorkLoop() const override;
    IONetworkInterface *createInterface() override;
    bool configureInterface(IONetworkInterface *netif) override;
    IOReturn enable(IONetworkInterface *netif) override;
    IOReturn disable(IONetworkInterface *netif) override;
    UInt32 outputPacket(mbuf_t m, void *param) override;
    IOReturn getHardwareAddress(IOEthernetAddress *addrP) override;
    IOReturn setHardwareAddress(const IOEthernetAddress *addrP) override;
    IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const override;
    IOReturn setPromiscuousMode(bool active) override;
    IOReturn setMulticastMode(bool active) override;
    IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) override;
    IOReturn selectMedium(const IONetworkMedium *medium) override;
    UInt32 getFeatures() const override;
    const OSString *newVendorString() const override;
    const OSString *newModelString() const override;

    // Power management
    IOReturn registerWithPolicyMaker(IOService *policyMaker) override;
    IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker) override;

    // IO80211Controller native hooks
    IOReturn getHardwareAddressForInterface(IO80211Interface *netif, IOEthernetAddress *addr) override;
    SInt32 apple80211Request(unsigned int requestType, int requestNumber,
                             IO80211Interface *interface, void *data) override;
    SInt32 stopDMA() override;
    UInt32 hardwareOutputQueueDepth(IO80211Interface *interface) override;
    SInt32 performCountryCodeOperation(IO80211Interface *interface, IO80211CountryCodeOp op) override;
    SInt32 enableFeature(IO80211FeatureCode code, void *data) override;
    bool useAppleRSNSupplicant(IO80211Interface *interface) override;
    int outputRaw80211Packet(IO80211Interface *interface, mbuf_t m) override;
    SInt32 monitorModeSetEnabled(IO80211Interface *interface, bool enabled, UInt32 dlt) override;

    // IStaEventListener callbacks (bridges MT7902Sta -> IO80211Interface::postMessage)
    void onDataFrameReceived(const uint8_t *ethFrame, size_t len) override;
    void onScanFinished() override;
    void onBssDiscovered(const mt7902::ScannedBss &bss) override;
    void onAssociationCompleted(bool success, uint16_t statusCode) override;
    void onLinkStateChanged(bool up) override;
    void onDeauthenticated(uint16_t reasonCode, bool isDisassoc) override;
    void onAssociatedBeacon(const mt7902::ScannedBss &bss) override;

private:
    static void interruptOccurred(OSObject *owner, IOInterruptEventSource *src, int count);
    static void watchdogFired(OSObject *owner, IOTimerEventSource *timer);
    bool createMediumTables(const IONetworkMedium **primary);
    void releaseResources();

    // IOCTL handlers
    IOReturn handleGetCardCapabilities(apple80211_capability_data *cap);
    IOReturn handleGetSupportedChannels(apple80211_sup_channel_data *ch);
    IOReturn handleGetScanResult(apple80211_scan_result **out);
    IOReturn handleSetScanReq(apple80211_scan_data *req);
    IOReturn handleSetScanReqMultiple(apple80211_scan_multiple_data *req);
    IOReturn handleSetAssociate(apple80211_assoc_data *req);
    IOReturn handleSetCipherKey(apple80211_key *key);

    IOPCIDevice *mPciNub = nullptr;
    IO80211WorkLoop *mWorkLoop = nullptr;
    IOCommandGate *mCommandGate = nullptr;
    IOInterruptEventSource *mInterruptSrc = nullptr;
    IOTimerEventSource *mWatchdogTimer = nullptr;
    AirportMT7902Interface *mNetIf = nullptr;
    IONetworkStats *mNetStats = nullptr;
    OSDictionary *mMediumDict = nullptr;

    mt7902::MT7902IOKitBus *mBus = nullptr;
    mt7902::MT7902Device *mDevice = nullptr;
    mt7902::MT7902Sta *mSta = nullptr;

    apple80211_scan_result mScanResultScratch = {};
    uint8_t mScanIeScratch[mt7902::kMaxRawIeLen] = {};
    uint32_t mPmState = 1;
};

#endif // KERNEL && !IO80211FAMILY_V2
#endif // AIRPORT_MT7902_CONTROLLER_V1_HPP
