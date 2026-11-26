// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Abstract PCIe MMIO + DMA + Timing bus layer (implemented by IOKit in macOS kext, mock in host tests)

#ifndef MT7902_BUS_HPP
#define MT7902_BUS_HPP

#include "../../include/MT7902Common.hpp"
#include "MT7902Regs.hpp"

namespace mt7902 {

struct DmaBuffer {
    void *virt = nullptr;
    uint64_t phys = 0;
    size_t size = 0;
    void *osHandle = nullptr; // IOBufferMemoryDescriptor* on macOS, unused in mock
};

class IMT7902Bus {
public:
    virtual ~IMT7902Bus() = default;

    // Raw BAR0 MMIO read/write (offset < BAR0 size)
    virtual uint32_t barRead32(uint32_t barOffset) = 0;
    virtual void barWrite32(uint32_t barOffset, uint32_t val) = 0;

    // Contiguous 32-bit-addressable DMA allocation (MT7902 uses DMA_BIT_MASK(32))
    virtual bool allocDma(size_t size, size_t align, DmaBuffer &out) = 0;
    virtual void freeDma(DmaBuffer &buf) = 0;
    virtual void syncDmaForDevice(const DmaBuffer &buf) = 0;
    virtual void syncDmaForCpu(const DmaBuffer &buf) = 0;

    // Timing & sleep
    virtual void delayUs(uint32_t us) = 0;
    virtual void sleepMs(uint32_t ms) = 0;
    virtual uint64_t nowMs() = 0;

    // PCIe config helpers
    virtual uint16_t pciVendorId() const = 0;
    virtual uint16_t pciDeviceId() const = 0;
    virtual bool isAspmSupported() const = 0;

    // Translated MT7902 register access (Fixed table + L1 remap window)
    uint32_t remapL1(uint32_t addr) {
        uint32_t offset = fieldGet(MT_HIF_REMAP_L1_OFFSET, addr);
        uint32_t base = fieldGet(MT_HIF_REMAP_L1_BASE, addr);
        uint32_t cur = barRead32(MT_HIF_REMAP_L1);
        cur = (cur & ~MT_HIF_REMAP_L1_MASK) | fieldPrep(MT_HIF_REMAP_L1_MASK, base);
        barWrite32(MT_HIF_REMAP_L1, cur);
        (void)barRead32(MT_HIF_REMAP_L1); // Posted-write barrier
        return MT_HIF_REMAP_BASE_L1 + offset;
    }

    uint32_t resolveAddr(uint32_t addr) {
        uint32_t mapped = 0;
        if (mt7902FixedMapLookup(addr, mapped))
            return mapped;
        if (mt7902NeedsL1Remap(addr))
            return remapL1(addr);
        MTK_ERR("Unsupported MT7902 register address 0x%08x", addr);
        return 0;
    }

    uint32_t rr(uint32_t addr) {
        return barRead32(resolveAddr(addr));
    }

    void wr(uint32_t addr, uint32_t val) {
        barWrite32(resolveAddr(addr), val);
    }

    uint32_t rmw(uint32_t addr, uint32_t clearMask, uint32_t setMask) {
        uint32_t barOfs = resolveAddr(addr);
        uint32_t v = barRead32(barOfs);
        v = (v & ~clearMask) | setMask;
        barWrite32(barOfs, v);
        return v;
    }

    void set(uint32_t addr, uint32_t bits) {
        rmw(addr, 0, bits);
    }

    void clear(uint32_t addr, uint32_t bits) {
        rmw(addr, bits, 0);
    }

    uint32_t l1Rr(uint32_t addr) {
        return barRead32(remapL1(addr));
    }

    void l1Wr(uint32_t addr, uint32_t val) {
        barWrite32(remapL1(addr), val);
    }

    bool pollMs(uint32_t addr, uint32_t mask, uint32_t expected,
                uint32_t timeoutMs, uint32_t intervalMs = 1) {
        uint64_t deadline = nowMs() + timeoutMs;
        while (true) {
            uint32_t v = rr(addr);
            if ((v & mask) == expected)
                return true;
            if (nowMs() >= deadline)
                return false;
            if (intervalMs > 0)
                sleepMs(intervalMs);
            else
                delayUs(100);
        }
    }

    bool pollL1Ms(uint32_t addr, uint32_t mask, uint32_t expected,
                  uint32_t timeoutMs, uint32_t intervalMs = 1) {
        uint64_t deadline = nowMs() + timeoutMs;
        while (true) {
            uint32_t v = l1Rr(addr);
            if ((v & mask) == expected)
                return true;
            if (nowMs() >= deadline)
                return false;
            if (intervalMs > 0)
                sleepMs(intervalMs);
            else
                delayUs(100);
        }
    }
};

} // namespace mt7902

#endif // MT7902_BUS_HPP
