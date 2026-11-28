// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// IEEE 802.11 RX A-MPDU Block Ack Reorder Buffer (matching Linux mt76/agg-rx.c)

#ifndef MT7902_RX_REORDER_HPP
#define MT7902_RX_REORDER_HPP

#include "../../include/MT7902Common.hpp"
#include "../HAL/MT7902Mac.hpp"

namespace mt7902 {

struct RxReorderStats {
    uint64_t inOrder = 0;
    uint64_t reordered = 0;
    uint64_t dupsDropped = 0;
    uint64_t timeouts = 0;
    uint64_t winAdvances = 0;
    uint64_t barsHandled = 0;
    uint64_t poolExhausted = 0;
};

class MT7902RxReorder {
public:
    static constexpr uint8_t kNumTids = 8;
    static constexpr uint16_t kMaxWinSize = 64;
    static constexpr size_t kPoolSize = 256;
    static constexpr size_t kMaxFrameLen = 2048;
    static constexpr uint16_t kSnMask = 0x0fffU;

    using DeliverFn = void (*)(const uint8_t *ethFrame, size_t len, void *ctx);

    MT7902RxReorder() = default;

    ~MT7902RxReorder() {
        resetAll();
        if (mPool) {
            kfreeSized(mPool, sizeof(ReorderPkt) * kPoolSize);
            mPool = nullptr;
            mFreeHead = nullptr;
        }
    }

    static inline uint16_t snInc(uint16_t sn) {
        return static_cast<uint16_t>((sn + 1U) & kSnMask);
    }

    static inline uint16_t snAdd(uint16_t sn, uint16_t delta) {
        return static_cast<uint16_t>((sn + delta) & kSnMask);
    }

    static inline uint16_t snSub(uint16_t a, uint16_t b) {
        return static_cast<uint16_t>((a - b) & kSnMask);
    }

    static inline bool snLess(uint16_t a, uint16_t b) {
        return ((a - b) & kSnMask) > 0x0800U;
    }

    static inline uint64_t tidTimeoutMs(uint8_t tid) {
        // Match Linux mt76_aggr_tid_to_timeo(): faster release for VI/VO, 80ms for BE/BK
        return (tid >= 4) ? 40ULL : 80ULL;
    }

    bool startSession(uint8_t tid, uint16_t ssn, uint16_t winSize,
                      DeliverFn deliver = nullptr, void *ctx = nullptr) {
        if (tid >= kNumTids)
            return false;
        if (!ensurePool())
            return false;

        stopSession(tid, deliver, ctx);

        if (winSize == 0 || winSize > kMaxWinSize)
            winSize = kMaxWinSize;

        TidState &t = mTids[tid];
        t.active = true;
        t.started = false;
        t.head = ssn & kSnMask;
        t.size = winSize;
        t.nframes = 0;
        t.inOrderAmsduActive = false;
        t.inOrderAmsduSeq = 0;
        t.inOrderAmsduMs = 0;
        for (uint16_t i = 0; i < kMaxWinSize; ++i)
            t.slots[i] = {};
        return true;
    }

    void stopSession(uint8_t tid, DeliverFn deliver = nullptr, void *ctx = nullptr) {
        if (tid >= kNumTids)
            return;
        TidState &t = mTids[tid];
        if (!t.active)
            return;

        t.inOrderAmsduActive = false;
        if (t.nframes > 0) {
            uint16_t sn = t.head;
            for (uint16_t i = 0; i < t.size && t.nframes > 0; ++i, sn = snInc(sn)) {
                releaseSlot(t, static_cast<uint16_t>(sn % t.size), deliver, ctx, false);
            }
        }
        t.active = false;
        t.started = false;
        t.nframes = 0;
    }

    void resetAll() {
        for (uint8_t tid = 0; tid < kNumTids; ++tid) {
            TidState &t = mTids[tid];
            if (!t.active)
                continue;
            for (uint16_t i = 0; i < kMaxWinSize; ++i) {
                freeChain(t.slots[i].head);
                t.slots[i] = {};
            }
            t.active = false;
            t.started = false;
            t.nframes = 0;
            t.inOrderAmsduActive = false;
        }
    }

    bool isSessionActive(uint8_t tid) const {
        return tid < kNumTids && mTids[tid].active;
    }

    bool hasPendingFrames() const {
        for (uint8_t tid = 0; tid < kNumTids; ++tid) {
            if (mTids[tid].active && (mTids[tid].nframes > 0 || mTids[tid].inOrderAmsduActive))
                return true;
        }
        return false;
    }

    // Process one unicast QoS Ethernet frame belonging to a BA-capable TID.
    // Delivers zero, one, or many frames in exact IEEE 802.11 sequence order.
    void processFrame(const uint8_t *ethFrame, size_t len, const ParsedRxFrame &meta,
                      uint64_t nowMs, DeliverFn deliver, void *ctx) {
        if (!ethFrame || len == 0 || !deliver)
            return;

        uint8_t tid = meta.tid;
        if (tid >= kNumTids || !mTids[tid].active || len > kMaxFrameLen) {
            deliver(ethFrame, len, ctx);
            return;
        }

        TidState &t = mTids[tid];
        uint16_t seqno = meta.seqNo & kSnMask;

        // If an in-order A-MSDU was being streamed directly and the new frame does NOT
        // continue it (different seqno, non-AMSDU, or start of a new A-MSDU), close it out first.
        if (t.inOrderAmsduActive &&
            (!meta.amsdu || meta.firstAmsdu || seqno != t.inOrderAmsduSeq)) {
            closeInOrderAmsdu(t, deliver, ctx);
        }

        uint16_t head = t.head;
        uint16_t size = t.size;
        bool less = snLess(seqno, head);

        if (!t.started) {
            if (less) {
                // Pre-BA frame arriving before SSN: deliver immediately without updating head
                mStats.inOrder++;
                deliver(ethFrame, len, ctx);
                return;
            }
            t.started = true;
        }

        if (less) {
            mStats.dupsDropped++;
            return;
        }

        // Fast path: in-order frame (zero copy to reorder pool!)
        if (seqno == head) {
            mStats.inOrder++;
            deliver(ethFrame, len, ctx);
            if (meta.amsdu && !meta.lastAmsdu) {
                t.inOrderAmsduActive = true;
                t.inOrderAmsduSeq = seqno;
                t.inOrderAmsduMs = nowMs;
            } else {
                t.inOrderAmsduActive = false;
                t.head = snInc(head);
                if (t.nframes > 0)
                    releaseHead(t, deliver, ctx);
            }
            return;
        }

        // Out-of-order frame: check if it exceeds the current [head, head + size) window
        if (!snLess(seqno, snAdd(head, size))) {
            uint16_t newHead = snInc(snSub(seqno, size));
            mStats.winAdvances++;
            releaseFramesUpTo(t, newHead, deliver, ctx);
        }

        uint16_t idx = static_cast<uint16_t>(seqno % size);
        Slot &slot = t.slots[idx];

        // If slot already occupied: either this is a continuation subframe of the same A-MSDU,
        // or it is a duplicate MPDU.
        if (slot.head != nullptr) {
            if (meta.amsdu && !meta.firstAmsdu && slot.collecting && slot.seqNo == seqno) {
                ReorderPkt *pkt = allocPkt(ethFrame, len, t, deliver, ctx);
                if (!pkt) {
                    deliver(ethFrame, len, ctx);
                    return;
                }
                slot.tail->next = pkt;
                slot.tail = pkt;
                if (meta.lastAmsdu)
                    slot.collecting = false;
                mStats.reordered++;
            } else {
                mStats.dupsDropped++;
            }
            return;
        }

        ReorderPkt *pkt = allocPkt(ethFrame, len, t, deliver, ctx);
        if (!pkt) {
            // Should not happen after emergency flush, fall back to direct delivery
            deliver(ethFrame, len, ctx);
            return;
        }

        slot.head = pkt;
        slot.tail = pkt;
        slot.seqNo = seqno;
        slot.timestampMs = nowMs;
        slot.collecting = (meta.amsdu && !meta.lastAmsdu);
        t.nframes++;
        mStats.reordered++;

        releaseHead(t, deliver, ctx);
    }

    // Handle IEEE 802.11 Block Ack Request (BAR) control frame
    void handleBar(uint8_t tid, uint16_t ssn, DeliverFn deliver, void *ctx) {
        if (tid >= kNumTids || !mTids[tid].active)
            return;
        TidState &t = mTids[tid];
        ssn &= kSnMask;
        if (t.inOrderAmsduActive)
            closeInOrderAmsdu(t, deliver, ctx);
        if (!t.started) {
            if (!snLess(ssn, t.head))
                t.head = ssn;
            return;
        }
        if (snLess(t.head, ssn)) {
            mStats.barsHandled++;
            releaseFramesUpTo(t, ssn, deliver, ctx);
            releaseHead(t, deliver, ctx);
        }
    }

    // Release any buffered frames that have exceeded the reorder timeout (matching mt76_rx_aggr_check_release)
    void checkTimeouts(uint64_t nowMs, DeliverFn deliver, void *ctx) {
        for (uint8_t tid = 0; tid < kNumTids; ++tid) {
            TidState &t = mTids[tid];
            if (!t.active)
                continue;
            uint64_t timeo = tidTimeoutMs(tid);

            if (t.inOrderAmsduActive && nowMs >= t.inOrderAmsduMs + timeo) {
                closeInOrderAmsdu(t, deliver, ctx);
            }

            if (t.nframes == 0)
                continue;

            uint16_t rem = t.nframes;
            uint16_t startHead = t.head;
            for (uint16_t i = 0; i < t.size && rem > 0; ++i) {
                uint16_t sn = snAdd(startHead, i);
                uint16_t idx = static_cast<uint16_t>(sn % t.size);
                Slot &slot = t.slots[idx];
                if (!slot.head)
                    continue;
                rem--;
                if (nowMs < slot.timestampMs + timeo)
                    continue;

                mStats.timeouts++;
                releaseFramesUpTo(t, slot.seqNo, deliver, ctx);
                releaseHead(t, deliver, ctx);
                break; // Re-evaluate remaining frames on next pass if needed
            }
        }
    }

    const RxReorderStats &stats() const { return mStats; }

private:
    struct ReorderPkt {
        ReorderPkt *next = nullptr;
        uint16_t len = 0;
        uint8_t data[kMaxFrameLen];
    };

    struct Slot {
        ReorderPkt *head = nullptr;
        ReorderPkt *tail = nullptr;
        uint64_t timestampMs = 0;
        uint16_t seqNo = 0;
        bool collecting = false;
    };

    struct TidState {
        bool active = false;
        bool started = false;
        uint16_t head = 0;
        uint16_t size = kMaxWinSize;
        uint16_t nframes = 0;
        bool inOrderAmsduActive = false;
        uint16_t inOrderAmsduSeq = 0;
        uint64_t inOrderAmsduMs = 0;
        Slot slots[kMaxWinSize] = {};
    };

    bool ensurePool() {
        if (mPool)
            return true;
        mPool = static_cast<ReorderPkt *>(kallocZero(sizeof(ReorderPkt) * kPoolSize));
        if (!mPool)
            return false;
        mFreeHead = nullptr;
        for (size_t i = kPoolSize; i > 0; --i) {
            mPool[i - 1].next = mFreeHead;
            mFreeHead = &mPool[i - 1];
        }
        return true;
    }

    ReorderPkt *popPool() {
        if (!mFreeHead)
            return nullptr;
        ReorderPkt *p = mFreeHead;
        mFreeHead = p->next;
        p->next = nullptr;
        return p;
    }

    void freeChain(ReorderPkt *head) {
        while (head) {
            ReorderPkt *next = head->next;
            head->next = mFreeHead;
            mFreeHead = head;
            head = next;
        }
    }

    ReorderPkt *allocPkt(const uint8_t *data, size_t len, TidState &activeTid,
                         DeliverFn deliver, void *ctx) {
        ReorderPkt *p = popPool();
        if (!p) {
            // Pool exhausted: force-release oldest occupied slot to reclaim space
            mStats.poolExhausted++;
            for (uint16_t i = 0; i < activeTid.size && activeTid.nframes > 0 && !mFreeHead; ++i) {
                uint16_t sn = snAdd(activeTid.head, i);
                uint16_t idx = static_cast<uint16_t>(sn % activeTid.size);
                if (activeTid.slots[idx].head) {
                    releaseFramesUpTo(activeTid, snInc(sn), deliver, ctx);
                    releaseHead(activeTid, deliver, ctx);
                    break;
                }
            }
            p = popPool();
        }
        if (!p)
            return nullptr;
        p->len = static_cast<uint16_t>(len);
        memcpy(p->data, data, len);
        p->next = nullptr;
        return p;
    }

    void releaseSlot(TidState &t, uint16_t idx, DeliverFn deliver, void *ctx, bool incHead) {
        if (incHead)
            t.head = snInc(t.head);
        Slot &slot = t.slots[idx];
        ReorderPkt *chain = slot.head;
        if (!chain)
            return;
        slot.head = nullptr;
        slot.tail = nullptr;
        slot.collecting = false;
        if (t.nframes > 0)
            t.nframes--;

        while (chain) {
            ReorderPkt *next = chain->next;
            if (deliver)
                deliver(chain->data, chain->len, ctx);
            chain->next = mFreeHead;
            mFreeHead = chain;
            chain = next;
        }
    }

    void releaseFramesUpTo(TidState &t, uint16_t targetHead, DeliverFn deliver, void *ctx) {
        targetHead &= kSnMask;
        while (snLess(t.head, targetHead)) {
            uint16_t idx = static_cast<uint16_t>(t.head % t.size);
            releaseSlot(t, idx, deliver, ctx, true);
        }
    }

    void releaseHead(TidState &t, DeliverFn deliver, void *ctx) {
        while (t.nframes > 0) {
            uint16_t idx = static_cast<uint16_t>(t.head % t.size);
            if (!t.slots[idx].head)
                break;
            if (t.slots[idx].collecting)
                break;
            releaseSlot(t, idx, deliver, ctx, true);
        }
    }

    void closeInOrderAmsdu(TidState &t, DeliverFn deliver, void *ctx) {
        t.inOrderAmsduActive = false;
        t.head = snInc(t.head);
        if (t.nframes > 0)
            releaseHead(t, deliver, ctx);
    }

    TidState mTids[kNumTids] = {};
    ReorderPkt *mPool = nullptr;
    ReorderPkt *mFreeHead = nullptr;
    RxReorderStats mStats;
};

} // namespace mt7902

#endif // MT7902_RX_REORDER_HPP
