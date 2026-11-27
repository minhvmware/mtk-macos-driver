// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#ifdef KERNEL

#include "MT7902BluetoothFirmware.hpp"
#include <IOKit/IOLib.h>

#define super IOService
OSDefineMetaClassAndStructors(MT7902BluetoothFirmware, IOService);

namespace {

class IOUSBHostBtTransport : public mt7902::bt::IBtUsbTransport {
public:
    IOUSBHostBtTransport(IOService *client, IOUSBHostDevice *dev)
        : mClient(client), mDev(dev) {}

    int vendorReadReg32(uint32_t reg, uint32_t &outVal) override {
        outVal = 0;
        uint8_t buf[4] = {};
        StandardUSB::DeviceRequest req = {};
        req.bmRequestType = 0xC0; // USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE
        req.bRequest = 0x63;
        req.wValue = static_cast<uint16_t>(reg >> 16);
        req.wIndex = static_cast<uint16_t>(reg & 0xffffU);
        req.wLength = sizeof(buf);

        uint32_t transferred = 0;
        IOReturn ret = mDev->deviceRequest(mClient, req, buf, transferred, 2000);
        if (ret != kIOReturnSuccess || transferred < 4)
            return mt7902::kErrIO;

        outVal = mt7902::getLe32(buf);
        return mt7902::kOk;
    }

    int uhwWriteReg32(uint32_t reg, uint32_t val) override {
        uint8_t buf[4] = {};
        mt7902::putLe32(buf, val);

        StandardUSB::DeviceRequest req = {};
        req.bmRequestType = 0x5E; // Exact MediaTek UHW control OUT bmRequestType from btmtk.c
        req.bRequest = 0x02;
        req.wValue = static_cast<uint16_t>(reg >> 16);
        req.wIndex = static_cast<uint16_t>(reg & 0xffffU);
        req.wLength = sizeof(buf);

        uint32_t transferred = 0;
        IOReturn ret = mDev->deviceRequest(mClient, req, buf, transferred, 2000);
        return (ret == kIOReturnSuccess) ? mt7902::kOk : mt7902::kErrIO;
    }

    int sendHciCmdControl(const uint8_t *hciCmdPkt, size_t len) override {
        if (!hciCmdPkt || len == 0 || len > 260)
            return mt7902::kErrInvalid;

        uint8_t bounce[260];
        memcpy(bounce, hciCmdPkt, len);

        StandardUSB::DeviceRequest req = {};
        req.bmRequestType = 0x20; // USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE
        req.bRequest = 0x00;
        req.wValue = 0;
        req.wIndex = 0;
        req.wLength = static_cast<uint16_t>(len);

        uint32_t transferred = 0;
        IOReturn ret = mDev->deviceRequest(mClient, req, bounce, transferred, 2000);
        return (ret == kIOReturnSuccess) ? mt7902::kOk : mt7902::kErrIO;
    }

    int pollWmtEventControl(uint8_t *outBuf, size_t maxLen, size_t &actualLen) override {
        actualLen = 0;
        if (!outBuf || maxLen == 0)
            return mt7902::kErrInvalid;

        StandardUSB::DeviceRequest req = {};
        req.bmRequestType = 0xC0; // USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE
        req.bRequest = 0x01;
        req.wValue = 48; // Exact MediaTek WMT event poll wValue from btmtk.c
        req.wIndex = 0;
        req.wLength = static_cast<uint16_t>(maxLen > 64 ? 64 : maxLen);

        uint32_t transferred = 0;
        IOReturn ret = mDev->deviceRequest(mClient, req, outBuf, transferred, 1000);
        if (ret != kIOReturnSuccess)
            return mt7902::kErrIO;

        actualLen = transferred;
        return mt7902::kOk;
    }

    void delayUs(uint32_t us) override { IODelay(us); }
    void sleepMs(uint32_t ms) override { IOSleep(ms); }

private:
    IOService *mClient;
    IOUSBHostDevice *mDev;
};

} // namespace

IOService *MT7902BluetoothFirmware::probe(IOService *provider, SInt32 *score) {
    if (!super::probe(provider, score))
        return nullptr;

    IOUSBHostDevice *usbDev = OSDynamicCast(IOUSBHostDevice, provider);
    if (!usbDev)
        return nullptr;

    return this;
}

static IOPMPowerState kBtPowerStates[2] = {
    { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 }
};

bool MT7902BluetoothFirmware::start(IOService *provider) {
    if (!super::start(provider))
        return false;

    IOUSBHostDevice *usbDev = OSDynamicCast(IOUSBHostDevice, provider);
    if (!usbDev) {
        super::stop(provider);
        return false;
    }

    PMinit();
    registerPowerDriver(this, kBtPowerStates, 2);
    provider->joinPMtree(this);
    makeUsable();

    if (OSDynamicCast(OSBoolean, usbDev->getProperty("MT7902BTFirmwareLoaded")) != kOSBooleanTrue) {
        // Defer USB WMT firmware download by 3.5s at boot so PCIe AirportMT7902
        // completes CONNSYS patch/RAM/EFUSE initialization before BT WMT runs.
        IOSleep(3500);
        if (!usbDev->open(this)) {
            MTK_BT_ERR("Failed to open IOUSBHostDevice for MT7902 Bluetooth");
            PMstop();
            super::stop(provider);
            return false;
        }

        IOUSBHostBtTransport transport(this, usbDev);
        int err = mt7902::bt::MT7902BtLoader::setupDevice(transport);
        if (err == mt7902::kOk) {
            usbDev->setProperty("MT7902BTFirmwareLoaded", kOSBooleanTrue);
            usbDev->setProperty("FirmwareLoaded", kOSBooleanTrue);
        }

        usbDev->close(this);
    }

    // Rename the composite USB node AND set USB Product Name / kUSBProductString so macOS 12–15+
    // bluetoothd GetProductAndVendorID() matches the internal USB HCI controller cleanly.
    usbDev->setProperty("USB Product Name", "Bluetooth USB Host Controller");
    usbDev->setProperty("kUSBProductString", "Bluetooth USB Host Controller");
    usbDev->setName("Bluetooth USB Host Controller");
    registerService();
    return true;
}

void MT7902BluetoothFirmware::stop(IOService *provider) {
    PMstop();
    super::stop(provider);
}

IOReturn MT7902BluetoothFirmware::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) {
    (void)powerStateOrdinal;
    (void)whatDevice;
    return IOPMAckImplied;
}

#endif // KERNEL
