// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// macOS IOKit IOPCIDevice MMIO + 32-bit DMA + Timing implementation of IMT7902Bus

#ifndef MT7902_IOKIT_BUS_HPP
#define MT7902_IOKIT_BUS_HPP

#ifdef KERNEL

#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSByteOrder.h>
#include <kern/clock.h>

#include "../HAL/MT7902Bus.hpp"

namespace mt7902 {

class MT7902IOKitBus : public IMT7902Bus {
public:
    MT7902IOKitBus();
    ~MT7902IOKitBus() override;

    bool attach(IOPCIDevice *pciDev);
    void detach();

    uint32_t barRead32(uint32_t barOffset) override;
    void barWrite32(uint32_t barOffset, uint32_t val) override;

    bool allocDma(size_t size, size_t align, DmaBuffer &out) override;
    void freeDma(DmaBuffer &buf) override;
    void syncDmaForDevice(const DmaBuffer &buf) override;
    void syncDmaForCpu(const DmaBuffer &buf) override;

    void delayUs(uint32_t us) override;
    void sleepMs(uint32_t ms) override;
    uint64_t nowMs() override;

    uint16_t pciVendorId() const override { return mVendorId; }
    uint16_t pciDeviceId() const override { return mDeviceId; }
    bool isAspmSupported() const override { return mAspmSupported; }

    IOPCIDevice *pciDevice() const { return mPciDev; }

private:
    IOPCIDevice *mPciDev = nullptr;
    IOMemoryMap *mBar0Map = nullptr;
    volatile uint8_t *mBar0Virt = nullptr;
    IOByteCount mBar0Len = 0;
    uint16_t mVendorId = 0;
    uint16_t mDeviceId = 0;
    bool mAspmSupported = false;
};

} // namespace mt7902

#endif // KERNEL
#endif // MT7902_IOKIT_BUS_HPP
