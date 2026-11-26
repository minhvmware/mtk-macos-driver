// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// macOS IOUSBHostDevice kext for MediaTek MT7902 USB Bluetooth firmware injection

#ifndef MT7902_BLUETOOTH_FIRMWARE_HPP
#define MT7902_BLUETOOTH_FIRMWARE_HPP

#ifdef KERNEL

#include <IOKit/IOService.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/StandardUSB.h>
#include "MT7902BtProtocol.hpp"

class MT7902BluetoothFirmware : public IOService {
    OSDeclareDefaultStructors(MT7902BluetoothFirmware)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) override;
};

#endif // KERNEL
#endif // MT7902_BLUETOOTH_FIRMWARE_HPP
