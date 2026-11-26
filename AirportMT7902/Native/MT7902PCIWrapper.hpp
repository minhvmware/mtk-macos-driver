// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// PCIe provider wrapper for macOS 14 Sonoma / 15 Sequoia / 26 Tahoe (IO80211FAMILY_V2)

#ifndef MT7902_PCI_WRAPPER_HPP
#define MT7902_PCI_WRAPPER_HPP

#ifdef KERNEL

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <Airport/Apple80211.h>

class MT7902PCIWrapper : public IOService {
    OSDeclareDefaultStructors(MT7902PCIWrapper)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOWorkLoop *getWorkLoop() const override;
    IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) override;

    IOPCIDevice *pciNub = nullptr;
};

#endif // KERNEL
#endif // MT7902_PCI_WRAPPER_HPP
