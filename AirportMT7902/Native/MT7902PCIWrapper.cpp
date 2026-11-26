// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#if defined(KERNEL) && defined(IO80211FAMILY_V2)

#include "MT7902PCIWrapper.hpp"
#include "../../include/MT7902Common.hpp"

#define super IOService
OSDefineMetaClassAndStructors(MT7902PCIWrapper, IOService);

static constexpr UInt8 kPciCapIdMsi = 0x05;
static constexpr UInt8 kPciCapIdMsix = 0x11;
static constexpr UInt8 kPciMsiFlagsOfs = 2;
static constexpr UInt16 kPciMsiEnable = 0x0001;
static constexpr UInt16 kPciMsixEnable = 0x8000;

static IOPMPowerState kWrapperPowerStates[2] = {
    { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 }
};

extern IO80211WorkQueue *gMT7902WorkQueue;

IOWorkLoop *MT7902PCIWrapper::getWorkLoop() const {
    return gMT7902WorkQueue;
}

IOService *MT7902PCIWrapper::probe(IOService *provider, SInt32 *score) {
    super::probe(provider, score);
    IOPCIDevice *device = OSDynamicCast(IOPCIDevice, provider);
    if (!device)
        return nullptr;

    UInt16 vendor = device->configRead16(kIOPCIConfigVendorID);
    UInt16 devId = device->configRead16(kIOPCIConfigDeviceID);
    if (vendor != mt7902::kMT7902VendorID || devId != mt7902::kMT7902DeviceID)
        return nullptr;

    // Prefer MSI over MSI-X for single-vector MT7902 WFDMA0 interrupt delivery
    UInt8 msixCap = 0;
    if (device->findPCICapability(kPciCapIdMsix, &msixCap) != 0 && msixCap != 0) {
        UInt16 ctrl = device->configRead16(msixCap + kPciMsiFlagsOfs);
        device->configWrite16(msixCap + kPciMsiFlagsOfs, ctrl & ~kPciMsixEnable);
    }

    UInt8 msiCap = 0;
    if (device->findPCICapability(kPciCapIdMsi, &msiCap) != 0 && msiCap != 0) {
        UInt16 ctrl = device->configRead16(msiCap + kPciMsiFlagsOfs);
        device->configWrite16(msiCap + kPciMsiFlagsOfs, ctrl | kPciMsiEnable);
    }

    pciNub = device;
    MTK_LOG("MT7902PCIWrapper matched PCI device %04x:%04x", vendor, devId);
    return this;
}

bool MT7902PCIWrapper::start(IOService *provider) {
    gMT7902WorkQueue = IO80211WorkQueue::workQueue();
    if (!gMT7902WorkQueue || !super::start(provider))
        return false;

    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    PMinit();
    registerPowerDriver(this, kWrapperPowerStates, 2);
    provider->joinPMtree(this);
    registerService();
    return true;
}

void MT7902PCIWrapper::stop(IOService *provider) {
    PMstop();
    super::stop(provider);
}

IOReturn MT7902PCIWrapper::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) {
    (void)powerStateOrdinal;
    (void)whatDevice;
    return IOPMAckImplied;
}

#endif // KERNEL && IO80211FAMILY_V2
