// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#ifdef KERNEL

#include "MT7902IOKitBus.hpp"

namespace mt7902 {

struct IOKitDmaHandle {
    IOBufferMemoryDescriptor *memDesc = nullptr;
    IODMACommand *dmaCmd = nullptr;
};

MT7902IOKitBus::MT7902IOKitBus() = default;

MT7902IOKitBus::~MT7902IOKitBus() {
    detach();
}

bool MT7902IOKitBus::attach(IOPCIDevice *pciDev) {
    if (!pciDev)
        return false;

    mPciDev = pciDev;
    mVendorId = pciDev->configRead16(kIOPCIConfigVendorID);
    mDeviceId = pciDev->configRead16(kIOPCIConfigDeviceID);

    pciDev->setMemoryEnable(true);
    pciDev->setBusMasterEnable(true);
    pciDev->setIOEnable(false);

    // Check PCIe capability for ASPM support
    UInt8 pcieCap = 0;
    UInt16 lnkCtl = 0;
    if (pciDev->findPCICapability(kIOPCIPCIExpressCapability, &pcieCap) != 0 && pcieCap != 0) {
        UInt32 lnkCap = pciDev->configRead32(pcieCap + 0x0c);
        mAspmSupported = ((lnkCap >> 10) & 0x3U) != 0;
        lnkCtl = pciDev->configRead16(pcieCap + 0x10);
    }
    UInt16 pciCmd = pciDev->configRead16(kIOPCIConfigCommand);
    UInt16 pciSta = pciDev->configRead16(kIOPCIConfigStatus);

    mBar0Map = pciDev->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!mBar0Map) {
        MTK_ERR("Failed to map MT7902 PCI BAR0");
        return false;
    }

    mBar0Virt = reinterpret_cast<volatile uint8_t *>(mBar0Map->getVirtualAddress());
    mBar0Len = mBar0Map->getLength();
    MTK_LOG("Mapped MT7902 BAR0 (0x%04x:0x%04x) len=%llu cmd=0x%04x sta=0x%04x lnkctl=0x%04x aspm=%d",
            mVendorId, mDeviceId, static_cast<unsigned long long>(mBar0Len),
            pciCmd, pciSta, lnkCtl, mAspmSupported ? 1 : 0);
    return mBar0Virt != nullptr && mBar0Len >= 0x100000;
}

void MT7902IOKitBus::detach() {
    if (mBar0Map) {
        mBar0Map->release();
        mBar0Map = nullptr;
    }
    mBar0Virt = nullptr;
    mBar0Len = 0;
    mPciDev = nullptr;
}

uint32_t MT7902IOKitBus::barRead32(uint32_t barOffset) {
    if (!mBar0Virt || barOffset + 4 > mBar0Len)
        return 0xffffffffU;
    return OSReadLittleInt32(mBar0Virt, barOffset);
}

void MT7902IOKitBus::barWrite32(uint32_t barOffset, uint32_t val) {
    if (!mBar0Virt || barOffset + 4 > mBar0Len)
        return;
    OSWriteLittleInt32(mBar0Virt, barOffset, val);
    OSSynchronizeIO();
}

bool MT7902IOKitBus::allocDma(size_t size, size_t align, DmaBuffer &out) {
    out = {};
    if (size == 0)
        return false;

    mach_vm_address_t physMask = 0x00000000FFFFF000ULL;
    if (align > 4096)
        physMask = ~(static_cast<mach_vm_address_t>(align) - 1ULL) & 0x00000000FFFFFFFFULL;

    IOBufferMemoryDescriptor *mem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task,
        kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapDefaultCache,
        size,
        physMask);
    if (!mem)
        return false;

    if (mem->prepare(kIODirectionInOut) != kIOReturnSuccess) {
        mem->release();
        return false;
    }

    IODMACommand *cmd = IODMACommand::withSpecification(
        kIODMACommandOutputHost64,
        32, // MT7902 PCIe DMA_BIT_MASK(32)
        0,
        IODMACommand::kMapped,
        0,
        static_cast<UInt32>(align ? align : 4));
    if (!cmd) {
        mem->complete();
        mem->release();
        return false;
    }

    if (cmd->setMemoryDescriptor(mem) != kIOReturnSuccess) {
        cmd->release();
        mem->complete();
        mem->release();
        return false;
    }

    UInt64 offset = 0;
    IODMACommand::Segment64 seg;
    UInt32 numSegs = 1;
    if (cmd->gen64IOVMSegments(&offset, &seg, &numSegs) != kIOReturnSuccess || numSegs != 1) {
        cmd->clearMemoryDescriptor();
        cmd->release();
        mem->complete();
        mem->release();
        return false;
    }

    auto *handle = static_cast<IOKitDmaHandle *>(kallocZero(sizeof(IOKitDmaHandle)));
    if (!handle) {
        cmd->clearMemoryDescriptor();
        cmd->release();
        mem->complete();
        mem->release();
        return false;
    }
    handle->memDesc = mem;
    handle->dmaCmd = cmd;

    out.virt = mem->getBytesNoCopy();
    out.phys = seg.fIOVMAddr;
    out.size = size;
    out.osHandle = handle;
    bzero(out.virt, size);
    return true;
}

void MT7902IOKitBus::freeDma(DmaBuffer &buf) {
    if (buf.osHandle) {
        auto *handle = static_cast<IOKitDmaHandle *>(buf.osHandle);
        if (handle->dmaCmd) {
            handle->dmaCmd->clearMemoryDescriptor();
            handle->dmaCmd->release();
        }
        if (handle->memDesc) {
            handle->memDesc->complete();
            handle->memDesc->release();
        }
        kfreeSized(handle, sizeof(IOKitDmaHandle));
    }
    buf = {};
}

void MT7902IOKitBus::syncDmaForDevice(const DmaBuffer &buf) {
    (void)buf;
    OSSynchronizeIO();
}

void MT7902IOKitBus::syncDmaForCpu(const DmaBuffer &buf) {
    (void)buf;
    OSSynchronizeIO();
}

void MT7902IOKitBus::delayUs(uint32_t us) {
    IODelay(us);
}

void MT7902IOKitBus::sleepMs(uint32_t ms) {
    IOSleep(ms);
}

uint64_t MT7902IOKitBus::nowMs() {
    uint64_t abstime = mach_absolute_time();
    uint64_t nsec = 0;
    absolutetime_to_nanoseconds(abstime, &nsec);
    return nsec / 1000000ULL;
}

} // namespace mt7902

#endif // KERNEL
