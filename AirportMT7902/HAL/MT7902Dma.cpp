// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902Dma.hpp"

namespace mt7902 {

MT7902Dma::MT7902Dma(IMT7902Bus &bus) : mBus(bus) {}

MT7902Dma::~MT7902Dma() {
    cleanup();
}

void MT7902Dma::configurePrefetch() {
    // Exact MT7902 WFDMA0 prefetch layout from Linux mt792x_dma.c (is_mt7902 branch)
    // RX rings 0..3
    mBus.wr(MT_WFDMA0_RX_RING0_EXT_CTRL, mtPrefetch(0x0000, 0x4));
    mBus.wr(MT_WFDMA0_RX_RING1_EXT_CTRL, mtPrefetch(0x0040, 0x4));
    mBus.wr(MT_WFDMA0_RX_RING2_EXT_CTRL, mtPrefetch(0x0080, 0x4));
    mBus.wr(MT_WFDMA0_RX_RING3_EXT_CTRL, mtPrefetch(0x00c0, 0x4));

    // TX rings 0..6, 15 (MT7902 MCU-WM), 16 (FWDL)
    mBus.wr(MT_WFDMA0_TX_RING0_EXT_CTRL, mtPrefetch(0x0100, 0x4));
    mBus.wr(MT_WFDMA0_TX_RING1_EXT_CTRL, mtPrefetch(0x0140, 0x4));
    mBus.wr(MT_WFDMA0_TX_RING2_EXT_CTRL, mtPrefetch(0x0180, 0x4));
    mBus.wr(MT_WFDMA0_TX_RING3_EXT_CTRL, mtPrefetch(0x01c0, 0x4));
    mBus.wr(MT_WFDMA0_TX_RING4_EXT_CTRL, mtPrefetch(0x0200, 0x4));
    mBus.wr(MT_WFDMA0_TX_RING5_EXT_CTRL, mtPrefetch(0x0240, 0x4));
    mBus.wr(MT_WFDMA0_TX_RING6_EXT_CTRL, mtPrefetch(0x0280, 0x4));
    mBus.wr(MT_WFDMA0_TX_RING15_EXT_CTRL, mtPrefetch(0x02c0, 0x4));
    mBus.wr(MT_WFDMA0_TX_RING16_EXT_CTRL, mtPrefetch(0x0300, 0x4));
}

int MT7902Dma::disable(bool forceReset) {
    mBus.clear(MT_WFDMA0_GLO_CFG,
               MT_WFDMA0_GLO_CFG_TX_DMA_EN |
               MT_WFDMA0_GLO_CFG_RX_DMA_EN |
               MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
               MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
               MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
               MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

    if (!mBus.pollMs(MT_WFDMA0_GLO_CFG,
                     MT_WFDMA0_GLO_CFG_TX_DMA_BUSY | MT_WFDMA0_GLO_CFG_RX_DMA_BUSY,
                     0, 100, 1)) {
        MTK_ERR("WFDMA0 disable timeout (GLO_CFG=0x%08x)", mBus.rr(MT_WFDMA0_GLO_CFG));
        return kErrTimeout;
    }

    // Bypass DMASHDL when disabling
    mBus.clear(MT_WFDMA0_GLO_CFG_EXT0, MT_WFDMA0_CSR_TX_DMASHDL_ENABLE);
    mBus.set(MT_DMASHDL_SW_CONTROL, MT_DMASHDL_DMASHDL_BYPASS);

    if (forceReset) {
        mBus.clear(MT_WFDMA0_RST,
                   MT_WFDMA0_RST_DMASHDL_ALL_RST | MT_WFDMA0_RST_LOGIC_RST);
        mBus.set(MT_WFDMA0_RST,
                 MT_WFDMA0_RST_DMASHDL_ALL_RST | MT_WFDMA0_RST_LOGIC_RST);
    }

    return kOk;
}

int MT7902Dma::enable() {
    configurePrefetch();

    // Reset DTX pointer
    mBus.wr(MT_WFDMA0_RST_DTX_PTR, ~0U);

    // Disable delay interrupt for lowest latency
    mBus.wr(MT_WFDMA0_PRI_DLY_INT_CFG0, 0);

    mBus.set(MT_WFDMA0_GLO_CFG,
             MT_WFDMA0_GLO_CFG_TX_WB_DDONE |
             MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN |
             MT_WFDMA0_GLO_CFG_CLK_GAT_DIS |
             MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
             fieldPrep(MT_WFDMA0_GLO_CFG_DMA_SIZE, 3) |
             MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK |
             MT_WFDMA0_GLO_CFG_RX_WB_DDONE |
             MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
             MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

    mBus.set(MT_WFDMA0_GLO_CFG,
             MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

    mBus.set(MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);

    enableInterrupts();
    mBus.set(MT_MCU2HOST_SW_INT_ENA, MT_MCU_CMD_WAKE_RX_PCIE);
    return kOk;
}

int MT7902Dma::allocRing(DmaQueueId qid, uint32_t regBase, uint16_t hwIdx,
                         uint16_t ndesc, uint32_t bufSize, bool isRx) {
    DmaRing &r = mRings[qid];
    r.regBase = regBase;
    r.hwIdx = hwIdx;
    r.ndesc = ndesc;
    r.head = 0;
    r.tail = 0;
    r.queued = 0;
    r.bufSize = bufSize;
    r.isRx = isRx;

    size_t descBytes = static_cast<size_t>(ndesc) * sizeof(Mt76Desc);
    if (!mBus.allocDma(descBytes, 64, r.descBuf)) {
        MTK_ERR("Failed to allocate DMA descriptor ring %u (%zu bytes)", qid, descBytes);
        return kErrNoMem;
    }
    memset(r.descBuf.virt, 0, descBytes);

    r.entries = static_cast<DmaRingEntry *>(kallocZero(sizeof(DmaRingEntry) * ndesc));
    if (!r.entries) {
        mBus.freeDma(r.descBuf);
        return kErrNoMem;
    }

    for (uint16_t i = 0; i < ndesc; ++i) {
        if (!mBus.allocDma(bufSize, 64, r.entries[i].buf)) {
            freeRing(qid);
            return kErrNoMem;
        }
        r.entries[i].inUse = false;
    }

    r.initialized = true;
    resetRingRegs(r);

    if (isRx) {
        rxRefill(qid);
    }

    return kOk;
}

void MT7902Dma::resetRingRegs(DmaRing &r) {
    if (!r.initialized)
        return;

    Mt76Desc *d = r.descs();
    for (uint16_t i = 0; i < r.ndesc; ++i) {
        d[i].buf0 = 0;
        d[i].buf1 = 0;
        d[i].info = 0;
        // Match Linux mt76_dma_queue_reset(): mark all descriptors DMA_DONE initially;
        // rxRefill() clears DMA_DONE on slots 0..ndesc-2 while keeping guard slot ndesc-1 owned by CPU.
        d[i].ctrl = MT_DMA_CTL_DMA_DONE;
        r.entries[i].inUse = false;
        r.entries[i].cookie = nullptr;
    }
    mBus.syncDmaForDevice(r.descBuf);

    r.head = 0;
    r.tail = 0;
    r.queued = 0;

    mBus.wr(r.regBase + MT_RING_REG_CPU_IDX, 0);
    mBus.wr(r.regBase + MT_RING_REG_DMA_IDX, 0);
    mBus.wr(r.regBase + MT_RING_REG_RING_SIZE, r.ndesc);
    mBus.wr(r.regBase + MT_RING_REG_DESC_BASE, static_cast<uint32_t>(r.descBuf.phys & 0xffffffffU));
}

void MT7902Dma::freeRing(DmaQueueId qid) {
    DmaRing &r = mRings[qid];
    if (r.entries) {
        for (uint16_t i = 0; i < r.ndesc; ++i) {
            if (r.entries[i].buf.virt)
                mBus.freeDma(r.entries[i].buf);
        }
        kfreeSized(r.entries, sizeof(DmaRingEntry) * r.ndesc);
        r.entries = nullptr;
    }
    if (r.descBuf.virt)
        mBus.freeDma(r.descBuf);
    r.initialized = false;
    r.ndesc = 0;
    r.head = r.tail = r.queued = 0;
}

int MT7902Dma::init() {
    int err = disable(true);
    if (err != kOk)
        return err;

    // 1. TX Band0 Data/Mgmt ring (hwIdx = 0)
    err = allocRing(kTxBand0, MT_TX_RING_BASE + MT7902_TXQ_BAND0 * MT_RING_STRIDE,
                    MT7902_TXQ_BAND0, MT7902_TX_RING_SIZE, 2048, false);
    if (err != kOk) goto fail;

    mBus.wr(MT_WFDMA0_TX_RING0_EXT_CTRL, 0x4);

    // 2. TX MCU-WM command ring (MT7902 QUIRK: hwIdx = 15, NOT 17!)
    err = allocRing(kTxMcuWm, MT_TX_RING_BASE + MT7902_TXQ_MCU_WM * MT_RING_STRIDE,
                    MT7902_TXQ_MCU_WM, MT7902_TX_MCU_RING_SIZE, 4096, false);
    if (err != kOk) goto fail;

    // 3. TX Firmware Download ring (hwIdx = 16)
    err = allocRing(kTxFwdl, MT_TX_RING_BASE + MT7902_TXQ_FWDL * MT_RING_STRIDE,
                    MT7902_TXQ_FWDL, MT7902_TX_FWDL_RING_SIZE, 4608, false);
    if (err != kOk) goto fail;

    // 4. RX MCU Event + TX-Free shared ring (MT7902 QUIRK: size 512, NO MCU_WA ring!)
    err = allocRing(kRxMcuEvent, MT_RX_EVENT_RING_BASE,
                    MT7902_RXQ_MCU_WM, MT7902_RX_MCU_RING_SIZE, MT7902_RX_BUF_SIZE, true);
    if (err != kOk) goto fail;

    // 5. RX Band0 Data/Mgmt ring (Ring 2 at MT_WFDMA0(0x520))
    err = allocRing(kRxData, MT_RX_DATA_RING_BASE,
                    MT7902_RXQ_DATA, MT7902_RX_DATA_RING_SIZE, MT7902_RX_BUF_SIZE, true);
    if (err != kOk) goto fail;

    return enable();

fail:
    cleanup();
    return err;
}

void MT7902Dma::cleanup() {
    disableInterrupts();
    (void)disable(true);
    for (int i = 0; i < kDmaQueueCount; ++i)
        freeRing(static_cast<DmaQueueId>(i));
}

int MT7902Dma::reset(bool forceReset) {
    int err = disable(forceReset);
    if (err != kOk)
        return err;

    for (int i = 0; i < kDmaQueueCount; ++i) {
        if (mRings[i].initialized) {
            resetRingRegs(mRings[i]);
            if (mRings[i].isRx)
                rxRefill(static_cast<DmaQueueId>(i));
        }
    }

    return enable();
}

int MT7902Dma::txSubmitCopy(DmaQueueId qid, const void *data, size_t len, bool kick) {
    if (qid >= kDmaQueueCount || !data || len == 0)
        return kErrInvalid;
    DmaRing &r = mRings[qid];
    if (!r.initialized || r.isRx || len > r.bufSize)
        return kErrInvalid;

    // Reclaim finished descriptors before checking capacity
    if (r.queued >= r.ndesc - 2) {
        if (!kick)
            txKick(qid);
        txCleanup(qid);
    }
    if (r.queued >= r.ndesc - 1)
        return kErrNoSpace;

    uint16_t idx = r.head;
    DmaRingEntry &ent = r.entries[idx];
    memcpy(ent.buf.virt, data, len);
    if (kick)
        mBus.syncDmaForDevice(ent.buf);

    Mt76Desc &desc = r.descs()[idx];
    desc.buf0 = static_cast<uint32_t>(ent.buf.phys & 0xffffffffU);
    desc.buf1 = 0;
    desc.info = fieldPrep(MT_DMA_CTL_SDP0_H, static_cast<uint32_t>(ent.buf.phys >> 32));
    desc.ctrl = fieldPrep(MT_DMA_CTL_SD_LEN0, static_cast<uint32_t>(len)) |
                MT_DMA_CTL_LAST_SEC0;

    ent.inUse = true;
    ent.cookie = nullptr;
    r.head = static_cast<uint16_t>((idx + 1U) % r.ndesc);
    r.queued++;

    if (kick) {
        mBus.syncDmaForDevice(r.descBuf);
        mBus.wr(r.regBase + MT_RING_REG_CPU_IDX, r.head);
    }
    return kOk;
}

void MT7902Dma::txKick(DmaQueueId qid) {
    if (qid >= kDmaQueueCount)
        return;
    DmaRing &r = mRings[qid];
    if (!r.initialized || r.isRx)
        return;
    mBus.syncDmaForDevice(r.descBuf);
    mBus.wr(r.regBase + MT_RING_REG_CPU_IDX, r.head);
}

int MT7902Dma::txSubmitDual(DmaQueueId qid, const void *hdr, size_t hdrLen,
                            uint64_t payloadPhys, size_t payloadLen, void *cookie) {
    if (qid >= kDmaQueueCount || !hdr || hdrLen == 0)
        return kErrInvalid;
    DmaRing &r = mRings[qid];
    if (!r.initialized || r.isRx || hdrLen > r.bufSize)
        return kErrInvalid;

    if (r.queued >= r.ndesc - 2)
        txCleanup(qid);
    if (r.queued >= r.ndesc - 1)
        return kErrNoSpace;

    uint16_t idx = r.head;
    DmaRingEntry &ent = r.entries[idx];
    memcpy(ent.buf.virt, hdr, hdrLen);
    mBus.syncDmaForDevice(ent.buf);

    Mt76Desc &desc = r.descs()[idx];
    desc.buf0 = static_cast<uint32_t>(ent.buf.phys & 0xffffffffU);
    if (payloadPhys != 0 && payloadLen > 0) {
        desc.buf1 = static_cast<uint32_t>(payloadPhys & 0xffffffffU);
        desc.info = fieldPrep(MT_DMA_CTL_SDP0_H, static_cast<uint32_t>(ent.buf.phys >> 32)) |
                    fieldPrep(MT_DMA_CTL_SDP1_H, static_cast<uint32_t>(payloadPhys >> 32));
        desc.ctrl = fieldPrep(MT_DMA_CTL_SD_LEN0, static_cast<uint32_t>(hdrLen)) |
                    fieldPrep(MT_DMA_CTL_SD_LEN1, static_cast<uint32_t>(payloadLen)) |
                    MT_DMA_CTL_LAST_SEC1;
    } else {
        desc.buf1 = 0;
        desc.info = fieldPrep(MT_DMA_CTL_SDP0_H, static_cast<uint32_t>(ent.buf.phys >> 32));
        desc.ctrl = fieldPrep(MT_DMA_CTL_SD_LEN0, static_cast<uint32_t>(hdrLen)) |
                    MT_DMA_CTL_LAST_SEC0;
    }
    mBus.syncDmaForDevice(r.descBuf);

    ent.inUse = true;
    ent.cookie = cookie;
    r.head = static_cast<uint16_t>((idx + 1U) % r.ndesc);
    r.queued++;

    mBus.wr(r.regBase + MT_RING_REG_CPU_IDX, r.head);
    return kOk;
}

uint32_t MT7902Dma::txCleanup(DmaQueueId qid, void (*onFree)(void *cookie, void *ctx), void *ctx) {
    if (qid >= kDmaQueueCount)
        return 0;
    DmaRing &r = mRings[qid];
    if (!r.initialized || r.isRx || r.queued == 0)
        return 0;

    // MT_WFDMA0_GLO_CFG_TX_WB_DDONE is enabled, so hardware writes MT_DMA_CTL_DMA_DONE
    // into host descriptor memory directly without needing a BAR0 DMA_IDX MMIO read.
    mBus.syncDmaForCpu(r.descBuf);
    uint32_t freed = 0;

    while (r.queued > 0) {
        Mt76Desc &desc = r.descs()[r.tail];
        if (!(desc.ctrl & MT_DMA_CTL_DMA_DONE))
            break;

        DmaRingEntry &ent = r.entries[r.tail];
        if (ent.cookie && onFree)
            onFree(ent.cookie, ctx);
        ent.cookie = nullptr;
        ent.inUse = false;

        r.tail = static_cast<uint16_t>((r.tail + 1U) % r.ndesc);
        r.queued--;
        freed++;
    }
    return freed;
}

int MT7902Dma::rxRefill(DmaQueueId qid) {
    if (qid >= kDmaQueueCount)
        return kErrInvalid;
    DmaRing &r = mRings[qid];
    if (!r.initialized || !r.isRx)
        return kErrInvalid;

    int refilled = 0;
    while (r.queued < r.ndesc - 1) {
        uint16_t idx = r.head;
        DmaRingEntry &ent = r.entries[idx];
        Mt76Desc &desc = r.descs()[idx];

        desc.buf0 = static_cast<uint32_t>(ent.buf.phys & 0xffffffffU);
        desc.buf1 = 0;
        desc.info = fieldPrep(MT_DMA_CTL_SDP0_H, static_cast<uint32_t>(ent.buf.phys >> 32));
        desc.ctrl = fieldPrep(MT_DMA_CTL_SD_LEN0, r.bufSize);
        ent.inUse = true;

        r.head = static_cast<uint16_t>((idx + 1U) % r.ndesc);
        r.queued++;
        refilled++;
    }

    if (refilled > 0) {
        mBus.syncDmaForDevice(r.descBuf);
        uint32_t cpuIdx = (r.head + r.ndesc - 1U) % r.ndesc;
        mBus.wr(r.regBase + MT_RING_REG_CPU_IDX, cpuIdx);
    }

    return refilled;
}

int MT7902Dma::rxDequeue(DmaQueueId qid, uint8_t *outBuf, size_t outCap, size_t &outLen,
                         bool autoRefill) {
    outLen = 0;
    if (qid >= kDmaQueueCount || !outBuf || outCap == 0)
        return kErrInvalid;
    DmaRing &r = mRings[qid];
    if (!r.initialized || !r.isRx || r.queued == 0)
        return kErrAgain;

    mBus.syncDmaForCpu(r.descBuf);
    Mt76Desc &desc = r.descs()[r.tail];
    uint32_t ctrl = desc.ctrl;
    if (!(ctrl & MT_DMA_CTL_DMA_DONE))
        return kErrAgain;

    uint32_t len = fieldGet(MT_DMA_CTL_SD_LEN0, ctrl);
    DmaRingEntry &ent = r.entries[r.tail];
    mBus.syncDmaForCpu(ent.buf);

    bool drop = (ctrl & (MT_DMA_CTL_TO_HOST_A | MT_DMA_CTL_DROP)) != 0;
    if (!drop && len > 0 && len <= outCap && len <= r.bufSize) {
        memcpy(outBuf, ent.buf.virt, len);
        outLen = len;
    }

    ent.inUse = false;
    r.tail = static_cast<uint16_t>((r.tail + 1U) % r.ndesc);
    r.queued--;

    if (autoRefill)
        rxRefill(qid);
    return (drop || outLen == 0) ? kErrAgain : kOk;
}

void MT7902Dma::syncRxDescForCpu(DmaQueueId qid) {
    if (qid >= kDmaQueueCount || !mRings[qid].initialized)
        return;
    mBus.syncDmaForCpu(mRings[qid].descBuf);
}

int MT7902Dma::rxPeek(DmaQueueId qid, const uint8_t *&outData, size_t &outLen, bool &wasDropped) {
    outData = nullptr;
    outLen = 0;
    wasDropped = false;
    if (qid >= kDmaQueueCount)
        return kErrInvalid;
    DmaRing &r = mRings[qid];
    if (!r.initialized || !r.isRx || r.queued == 0)
        return kErrAgain;

    const volatile uint32_t *ctrlPtr = &r.descs()[r.tail].ctrl;
    uint32_t ctrl = *ctrlPtr;
    if (!(ctrl & MT_DMA_CTL_DMA_DONE))
        return kErrAgain;

    uint32_t len = fieldGet(MT_DMA_CTL_SD_LEN0, ctrl);
    DmaRingEntry &ent = r.entries[r.tail];
    bool drop = (ctrl & (MT_DMA_CTL_TO_HOST_A | MT_DMA_CTL_DROP)) != 0;
    if (drop || len == 0 || len > r.bufSize) {
        ent.inUse = false;
        r.tail = static_cast<uint16_t>((r.tail + 1U) % r.ndesc);
        r.queued--;
        wasDropped = true;
        return kErrAgain;
    }

    outData = static_cast<const uint8_t *>(ent.buf.virt);
    outLen = len;
    return kOk;
}

void MT7902Dma::rxConsume(DmaQueueId qid) {
    if (qid >= kDmaQueueCount)
        return;
    DmaRing &r = mRings[qid];
    if (!r.initialized || !r.isRx || r.queued == 0)
        return;
    r.entries[r.tail].inUse = false;
    r.tail = static_cast<uint16_t>((r.tail + 1U) % r.ndesc);
    r.queued--;
}

void MT7902Dma::setIrqMask(uint32_t clearBits, uint32_t setBits) {
    mIrqMask = (mIrqMask & ~clearBits) | setBits;
    mBus.wr(MT_WFDMA0_HOST_INT_ENA, mIrqMask);
}

void MT7902Dma::disableInterrupts() {
    mBus.wr(MT_WFDMA0_HOST_INT_ENA, 0);
    mBus.wr(MT_PCIE_MAC_INT_ENABLE, 0);
}

void MT7902Dma::enableInterrupts() {
    mBus.wr(MT_PCIE_MAC_INT_ENABLE, 0xff);
    setIrqMask(0, MT_INT_TX_DONE_ALL | MT_INT_RX_DONE_MT7902_ALL | MT_INT_MCU_CMD);
}

IrqStatus MT7902Dma::ackInterrupt() {
    IrqStatus st;
    mBus.wr(MT_WFDMA0_HOST_INT_ENA, 0);

    uint32_t intr = mBus.rr(MT_WFDMA0_HOST_INT_STA);
    intr &= mIrqMask;
    if (intr)
        mBus.wr(MT_WFDMA0_HOST_INT_STA, intr);

    if (intr & MT_INT_MCU_CMD) {
        uint32_t sw = mBus.rr(MT_MCU_CMD);
        mBus.wr(MT_MCU_CMD, sw);
        if (sw & MT_MCU_CMD_WAKE_RX_PCIE) {
            st.mcuWakeRx = true;
            intr |= MT_INT_RX_DONE_DATA;
        }
        if (sw & MT_MCU_CMD_STOP_DMA) {
            st.mcuStopDma = true;
        }
    }

    st.raw = intr;
    st.txDone = (intr & MT_INT_TX_DONE_ALL) != 0;
    st.rxMcuEvent = (intr & MT_INT_RX_DONE_WM) != 0;
    st.rxData = (intr & MT_INT_RX_DONE_DATA) != 0;
    return st;
}

void MT7902Dma::clearPendingDataIrqs() {
    uint32_t intr = mBus.rr(MT_WFDMA0_HOST_INT_STA) &
                    (MT_INT_RX_DONE_MT7902_ALL | MT_INT_TX_DONE_ALL);
    if (intr)
        mBus.wr(MT_WFDMA0_HOST_INT_STA, intr);
}

void MT7902Dma::unmaskInterrupts() {
    mBus.wr(MT_WFDMA0_HOST_INT_ENA, mIrqMask);
}

IrqStatus MT7902Dma::handleInterrupt() {
    IrqStatus st = ackInterrupt();
    unmaskInterrupts();
    return st;
}

bool MT7902Dma::needsReinit() const {
    return (mBus.rr(MT_WFDMA_DUMMY_CR) & MT_WFDMA_NEED_REINIT) == 0;
}

} // namespace mt7902
