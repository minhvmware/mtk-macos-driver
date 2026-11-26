// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// WFDMA0 ring buffer manager tailored to MediaTek MT7902 (Filogic 310)

#ifndef MT7902_DMA_HPP
#define MT7902_DMA_HPP

#include "../../include/MT7902Common.hpp"
#include "MT7902Bus.hpp"
#include "MT7902DmaTypes.hpp"
#include "MT7902Regs.hpp"

namespace mt7902 {

enum DmaQueueId : uint8_t {
    kTxBand0 = 0,
    kTxMcuWm = 1,
    kTxFwdl = 2,
    kRxMcuEvent = 3,
    kRxData = 4,
    kDmaQueueCount = 5,
};

struct DmaRingEntry {
    DmaBuffer buf;
    void *cookie = nullptr; // optional mbuf_t / token handle
    bool inUse = false;
};

struct DmaRing {
    uint32_t regBase = 0;     // BAR0 register base (MT_TX_RING_BASE + hwIdx*0x10 or MT_RX_*_RING_BASE)
    uint16_t hwIdx = 0;
    uint16_t ndesc = 0;
    uint16_t head = 0;        // CPU write/submit index
    uint16_t tail = 0;        // Reclaim / consume index
    uint16_t queued = 0;
    uint32_t bufSize = 0;
    bool isRx = false;
    bool initialized = false;

    DmaBuffer descBuf;        // Contiguous Mt76Desc[ndesc]
    DmaRingEntry *entries = nullptr;

    Mt76Desc *descs() const {
        return static_cast<Mt76Desc *>(descBuf.virt);
    }
};

struct IrqStatus {
    uint32_t raw = 0;
    bool txDone = false;
    bool rxMcuEvent = false;
    bool rxData = false;
    bool mcuWakeRx = false;
};

class MT7902Dma {
public:
    explicit MT7902Dma(IMT7902Bus &bus);
    ~MT7902Dma();

    int init();
    void cleanup();

    int disable(bool forceReset);
    int enable();
    int reset(bool forceReset);

    // Program MT7902-specific prefetch table (RX0..3, TX0..6, TX15, TX16)
    void configurePrefetch();

    // TX operations: submit a single linear buffer (copied into ring DMA buffer) or 2-segment TXWI+payload
    int txSubmitCopy(DmaQueueId qid, const void *data, size_t len);
    int txSubmitDual(DmaQueueId qid, const void *hdr, size_t hdrLen,
                     uint64_t payloadPhys, size_t payloadLen, void *cookie);
    uint32_t txCleanup(DmaQueueId qid, void (*onFree)(void *cookie, void *ctx) = nullptr, void *ctx = nullptr);

    // RX operations: refill ring descriptors and dequeue completed packets
    int rxRefill(DmaQueueId qid);
    int rxDequeue(DmaQueueId qid, uint8_t *outBuf, size_t outCap, size_t &outLen);

    // Interrupt handling
    void setIrqMask(uint32_t clearBits, uint32_t setBits);
    void disableInterrupts();
    void enableInterrupts();
    IrqStatus handleInterrupt();

    bool needsReinit() const;
    const DmaRing &ring(DmaQueueId qid) const { return mRings[qid]; }
    uint32_t irqMask() const { return mIrqMask; }

private:
    int allocRing(DmaQueueId qid, uint32_t regBase, uint16_t hwIdx,
                  uint16_t ndesc, uint32_t bufSize, bool isRx);
    void freeRing(DmaQueueId qid);
    void resetRingRegs(DmaRing &r);

    IMT7902Bus &mBus;
    DmaRing mRings[kDmaQueueCount];
    uint32_t mIrqMask = 0;
};

} // namespace mt7902

#endif // MT7902_DMA_HPP
