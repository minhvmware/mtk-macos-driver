// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#ifndef MT7902_COMMON_HPP
#define MT7902_COMMON_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef KERNEL
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#define MTK_LOG(fmt, ...) IOLog("AirportMT7902: " fmt "\n", ##__VA_ARGS__)
#define MTK_ERR(fmt, ...) IOLog("AirportMT7902 [ERR]: " fmt "\n", ##__VA_ARGS__)
#ifdef DEBUG
#define MTK_DBG(fmt, ...) IOLog("AirportMT7902 [DBG]: " fmt "\n", ##__VA_ARGS__)
#else
#define MTK_DBG(fmt, ...) do {} while (0)
#endif
#define MTK_BT_LOG(fmt, ...) IOLog("MT7902BT: " fmt "\n", ##__VA_ARGS__)
#define MTK_BT_ERR(fmt, ...) IOLog("MT7902BT [ERR]: " fmt "\n", ##__VA_ARGS__)
#else
#include <stdio.h>
#include <stdlib.h>
#define MTK_LOG(fmt, ...) fprintf(stdout, "[MT7902] " fmt "\n", ##__VA_ARGS__)
#define MTK_ERR(fmt, ...) fprintf(stderr, "[MT7902 ERR] " fmt "\n", ##__VA_ARGS__)
#ifdef DEBUG
#define MTK_DBG(fmt, ...) fprintf(stdout, "[MT7902 DBG] " fmt "\n", ##__VA_ARGS__)
#else
#define MTK_DBG(fmt, ...) do {} while (0)
#endif
#define MTK_BT_LOG(fmt, ...) fprintf(stdout, "[MT7902BT] " fmt "\n", ##__VA_ARGS__)
#define MTK_BT_ERR(fmt, ...) fprintf(stderr, "[MT7902BT ERR] " fmt "\n", ##__VA_ARGS__)
#endif

namespace mt7902 {

#ifndef MTK_BIT
#define MTK_BIT(n) (1U << (n))
#endif

#ifndef MTK_BIT64
#define MTK_BIT64(n) (1ULL << (n))
#endif

// Inclusive bitmask [hi:lo] for 32-bit registers
static constexpr uint32_t genmask32(unsigned hi, unsigned lo) {
    return ((~0U - (1U << lo) + 1U) & (~0U >> (31U - hi)));
}

static constexpr unsigned maskShift(uint32_t mask) {
    unsigned shift = 0;
    while (mask && ((mask & 1U) == 0U)) {
        mask >>= 1;
        ++shift;
    }
    return shift;
}

static constexpr uint32_t fieldPrep(uint32_t mask, uint32_t val) {
    return (val << maskShift(mask)) & mask;
}

static constexpr uint32_t fieldGet(uint32_t mask, uint32_t reg) {
    return (reg & mask) >> maskShift(mask);
}

static inline uint16_t getLe16(const void *p) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    return static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
}

static inline uint32_t getLe32(const void *p) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    return static_cast<uint32_t>(b[0]) |
           (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
}

static inline uint16_t getBe16(const void *p) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    return (static_cast<uint16_t>(b[0]) << 8) | static_cast<uint16_t>(b[1]);
}

static inline uint32_t getBe32(const void *p) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    return (static_cast<uint32_t>(b[0]) << 24) |
           (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) |
           static_cast<uint32_t>(b[3]);
}

static inline void putLe16(void *p, uint16_t v) {
    uint8_t *b = static_cast<uint8_t *>(p);
    b[0] = static_cast<uint8_t>(v & 0xffU);
    b[1] = static_cast<uint8_t>((v >> 8) & 0xffU);
}

static inline void putLe32(void *p, uint32_t v) {
    uint8_t *b = static_cast<uint8_t *>(p);
    b[0] = static_cast<uint8_t>(v & 0xffU);
    b[1] = static_cast<uint8_t>((v >> 8) & 0xffU);
    b[2] = static_cast<uint8_t>((v >> 16) & 0xffU);
    b[3] = static_cast<uint8_t>((v >> 24) & 0xffU);
}

static inline void putBe16(void *p, uint16_t v) {
    uint8_t *b = static_cast<uint8_t *>(p);
    b[0] = static_cast<uint8_t>((v >> 8) & 0xffU);
    b[1] = static_cast<uint8_t>(v & 0xffU);
}

static inline void putBe32(void *p, uint32_t v) {
    uint8_t *b = static_cast<uint8_t *>(p);
    b[0] = static_cast<uint8_t>((v >> 24) & 0xffU);
    b[1] = static_cast<uint8_t>((v >> 16) & 0xffU);
    b[2] = static_cast<uint8_t>((v >> 8) & 0xffU);
    b[3] = static_cast<uint8_t>(v & 0xffU);
}

static inline uint32_t popcount8(uint8_t v) {
    uint32_t c = 0;
    while (v) {
        c += v & 1U;
        v >>= 1;
    }
    return c;
}

static inline void *kallocZero(size_t sz) {
#ifdef KERNEL
    void *p = IOMalloc(sz);
    if (p)
        bzero(p, sz);
    return p;
#else
    return calloc(1, sz);
#endif
}

static inline void kfreeSized(void *p, size_t sz) {
    if (!p)
        return;
#ifdef KERNEL
    IOFree(p, sz);
#else
    (void)sz;
    free(p);
#endif
}

static constexpr uint16_t kMT7902VendorID = 0x14c3;
static constexpr uint16_t kMT7902DeviceID = 0x7902;

static constexpr size_t kEthAddrLen = 6;
static constexpr size_t kEthHdrLen = 14;
static constexpr size_t kMaxSsidLen = 32;

// Negative POSIX-style error codes shared across kernel & host HAL
enum Err : int {
    kOk = 0,
    kErrInvalid = -22,      // EINVAL
    kErrNoMem = -12,        // ENOMEM
    kErrIO = -5,            // EIO
    kErrTimeout = -60,      // ETIMEDOUT
    kErrBusy = -16,         // EBUSY
    kErrAgain = -35,        // EAGAIN
    kErrNotFound = -2,      // ENOENT
    kErrNotSupp = -45,      // EOPNOTSUPP
    kErrNoDev = -19,        // ENODEV
    kErrNoSpace = -28,      // ENOSPC
};

} // namespace mt7902

#endif // MT7902_COMMON_HPP
