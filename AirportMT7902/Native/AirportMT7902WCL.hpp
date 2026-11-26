// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// macOS 15 Sequoia & macOS 26 Tahoe native WCL wire-format structures
// ABI layouts cross-checked against laobamac/itlwm WCL.hpp

#ifndef AIRPORT_MT7902_WCL_HPP
#define AIRPORT_MT7902_WCL_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace mt7902 {
namespace wcl {

enum BeaconFlags : uint32_t {
    kSsidPresent = 1U << 1,
    kRssiValid = 1U << 14,
};

enum MessageId : uint32_t {
    kMsgLqmUpdate = 39,
    kMsgResetInterface = 49,
    kMsgDriverAvailable = 55,
    kMsgScanResult = 201,
    kMsgDeauth = 203,
    kMsgAssocComplete = 211,
    kMsgFirstBeacon = 212,
    kMsgConnectComplete = 213,
    kMsgJoinAbortComplete = 214,
    kMsgLinkStatus = 216,
    kMsgScanDone = 237,
};

struct HardwareAddress {
    uint32_t version;
    uint8_t address[6];
    uint8_t padding[2];
};
static_assert(sizeof(HardwareAddress) == 12, "HW_ADDR wire size mismatch");
static_assert(offsetof(HardwareAddress, address) == 4, "HW_ADDR offset mismatch");

struct DriverAvailability {
    uint32_t version;
    uint32_t reserved0;
    uint32_t available;
    uint32_t reserved1;
    uint32_t reason;
    uint32_t subReason;
    uint8_t reserved2[0xf8 - 24];
};
static_assert(sizeof(DriverAvailability) == 0xf8, "DriverAvailability size mismatch");

struct BeaconMetadata {
    uint32_t ieLength;
    uint16_t channelSpec;
    uint8_t ssid[32];
    uint8_t ssidLength;
    uint8_t channel;
    uint8_t reserved0;
    uint8_t bssid[6];
    uint8_t reserved1;
    int32_t rssi;
    int16_t noise;
    int16_t snr;
    uint16_t interval;
    uint16_t capability;
#if !defined(__IO80211_TARGET) || __IO80211_TARGET != __MAC_15_2
    uint32_t reserved2;
#endif
    uint32_t flags;
};

#if defined(__IO80211_TARGET) && __IO80211_TARGET == __MAC_15_2
static_assert(sizeof(BeaconMetadata) == 0x40, "Sequoia BeaconMetadata size mismatch");
static_assert(offsetof(BeaconMetadata, flags) == 0x3c, "Sequoia BeaconMetadata flags offset");
#else
static_assert(sizeof(BeaconMetadata) == 0x44, "Tahoe BeaconMetadata size mismatch");
static_assert(offsetof(BeaconMetadata, flags) == 0x40, "Tahoe BeaconMetadata flags offset");
#endif
static_assert(offsetof(BeaconMetadata, bssid) == 0x29, "BeaconMetadata BSSID offset");
static_assert(offsetof(BeaconMetadata, rssi) == 0x30, "BeaconMetadata RSSI offset");

struct AssociationEvent {
    uint16_t status;
    uint16_t reason;
    uint8_t mlo;
    uint8_t bssid[6];
    uint8_t reserved;
    uint32_t mloData[2];
    int32_t driverStatus;
    int32_t driverReason;
};
static_assert(sizeof(AssociationEvent) == 0x1c, "AssociationEvent size mismatch");

struct ConnectionPeer {
    uint8_t bssid[6];
    uint16_t reserved;
    int32_t driverStatus;
    int32_t driverReason;
};

struct FirstBeaconEvent {
    uint16_t status;
    uint16_t reason;
    ConnectionPeer peer;
};
static_assert(sizeof(FirstBeaconEvent) == 0x14, "FirstBeaconEvent size mismatch");

struct ConnectionEvent {
    uint16_t status;
    uint16_t reason;
    ConnectionPeer peers[10];
};
static_assert(sizeof(ConnectionEvent) == 0xa4, "ConnectionEvent size mismatch");

struct LinkStatusEvent {
    uint8_t bssid[6];
    uint8_t up;
    uint8_t inSleep;
    uint32_t reason;
    uint32_t reserved;
};
static_assert(sizeof(LinkStatusEvent) == 16, "LinkStatusEvent size mismatch");

struct DeauthEvent {
    int32_t driverStatus;
    int32_t driverReason;
    uint16_t reason;
    uint8_t bssid[6];
    uint8_t disassociation;
    uint8_t reserved[3];
};
static_assert(sizeof(DeauthEvent) == 0x14, "DeauthEvent size mismatch");

struct __attribute__((packed)) ChannelEntry {
    uint16_t channelSpec;
    uint8_t reserved[4];
    uint8_t channel;
    uint8_t flags;
};

struct __attribute__((packed)) ChannelInfo {
    ChannelEntry channels[400];
    uint16_t count;
    uint8_t country[3];
    uint8_t supports6GHz;
};
static_assert(sizeof(ChannelInfo) == 0xc86, "ChannelInfo size mismatch");

struct NativeChannelInfo {
    uint8_t reserved0[8];
    uint8_t country[4];
    uint16_t count;
    uint16_t channelSpec[400];
    uint8_t channel[400];
    uint8_t indoor[400];
    uint8_t dfs[400];
    uint8_t passive[400];
    uint8_t supports40MHz[400];
    uint8_t supports80MHz[400];
    uint8_t reserved1[2];
    uint32_t firmwareChannelSpec[400];
    uint32_t channelBitmap[400];
    uint8_t reserved2[0x2720 - 0x1910];
};
static_assert(sizeof(NativeChannelInfo) == 0x2720, "NativeChannelInfo size mismatch");

struct TrafficCounters {
    uint64_t realtimeInfraTx;
    uint64_t realtimeInfraRx;
    uint64_t infraTx;
    uint64_t infraRx;
    uint64_t realtimeAwdlTx;
    uint64_t realtimeNanTx;
    uint64_t timestampNanoseconds;
};
static_assert(sizeof(TrafficCounters) == 0x38, "TrafficCounters size mismatch");

struct SequoiaLqmUpdate {
    uint8_t rssiValid;
    uint8_t reserved0[3];
    int32_t rssi;
    uint8_t reserved1[0x150 - 8];
};
static_assert(sizeof(SequoiaLqmUpdate) == 0x150, "SequoiaLqmUpdate size mismatch");

struct LqmBeaconUpdate {
    uint8_t prefix[0x28];
    uint32_t receivedDelta;
    uint32_t scheduledDelta;
    uint8_t beaconValid;
    uint8_t reserved[0x1d8 - 0x31];
    uint8_t snapshotValid;
    uint8_t countersValid;
    uint8_t tail[2];
};
static_assert(sizeof(LqmBeaconUpdate) == 0x1dc, "LqmBeaconUpdate size mismatch");

struct DecodedScanRequest {
    uint32_t identifier;
    uint32_t type;
    uint8_t ssid[32];
    uint32_t ssidLength;
    uint8_t bssid[6];
    uint32_t channelCount;
    uint8_t channels[32];
};

struct DecodedJoinRequest {
    uint32_t lowerAuth;
    uint32_t upperAuth;
    uint32_t ssidLength;
    uint8_t ssid[32];
    uint32_t keyLength;
    uint32_t keyCipher;
    uint16_t keyIndex;
    uint8_t key[32];
    uint8_t bssid[6];
    uint16_t channelSpec;
    uint16_t rsnLength;
    uint8_t rsn[257];
};

static inline uint16_t rd16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static inline uint32_t rd32(const uint8_t *p) {
    return static_cast<uint32_t>(rd16(p)) | (static_cast<uint32_t>(rd16(p + 2)) << 16);
}

static inline bool decodeScan(const void *buffer, size_t length, DecodedScanRequest &out) {
    if (!buffer || length < 0x58)
        return false;
    const uint8_t *p = static_cast<const uint8_t *>(buffer);
    uint32_t count = rd32(p + 0x54);
    uint32_t ssidLen = rd32(p + 0x1c);
    uint32_t type = rd32(p + 0x40);
    if (count > 400 || ssidLen > 32 || type < 1 || type > 4 ||
        length < 0x58 + static_cast<size_t>(count) * 12)
        return false;

    out = {};
    out.identifier = rd32(p);
    out.type = type;
    out.ssidLength = ssidLen;
    out.channelCount = count;
    memcpy(out.ssid, p + 0x20, ssidLen);
    memcpy(out.bssid, p + 0x14, 6);

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *entry = p + 0x58 + static_cast<size_t>(i) * 12;
        uint32_t ch = rd32(entry + 4);
        uint32_t flags = rd32(entry + 8);
        if (!ch || ch > 196 || (flags & 0x2000) ||
            ((flags & 0x8) && ch > 14) || ((flags & 0x10) && ch <= 14))
            continue;
        out.channels[ch / 8] |= static_cast<uint8_t>(1U << (ch % 8));
    }
    return true;
}

static inline bool decodeJoin(const void *buffer, size_t length, DecodedJoinRequest &out) {
    if (!buffer || length < 0x218)
        return false;
    const uint8_t *p = static_cast<const uint8_t *>(buffer);
    uint32_t count = rd32(p + 0x214);
    uint32_t ssidLen = rd32(p + 0x1c);
    uint32_t keyLen = rd32(p + 0x44);
    uint16_t rsnLen = rd16(p + 0xd4);
    if (!count || count > 10 || !ssidLen || ssidLen > 32 ||
        keyLen > 32 || rsnLen > 257 || length < 0x218 + static_cast<size_t>(count) * 18)
        return false;
    if (p[0x21c] & 1)
        return false;

    out = {};
    out.lowerAuth = rd32(p + 0x10);
    out.upperAuth = rd32(p + 0x14);
    out.ssidLength = ssidLen;
    memcpy(out.ssid, p + 0x20, ssidLen);
    out.keyLength = keyLen;
    out.keyCipher = rd32(p + 0x48);
    out.keyIndex = rd16(p + 0x4e);
    memcpy(out.key, p + 0x50, keyLen);
    memcpy(out.bssid, p + 0x21c, 6);
    out.channelSpec = rd16(p + 0x228);
    out.rsnLength = rsnLen;
    memcpy(out.rsn, p + 0xd6, rsnLen);
    return true;
}

static inline bool appendNativeChannel(NativeChannelInfo &info, unsigned channel,
                                       bool fiveGHz, bool dfs, bool passive,
                                       bool supports40MHz, bool supports80MHz) {
    if (!channel || channel > 255 || info.count >= 400)
        return false;
    unsigned idx = info.count++;
    uint16_t spec = static_cast<uint16_t>(channel | (fiveGHz ? 0xd000U : 0x1000U));
    info.channelSpec[idx] = spec;
    info.firmwareChannelSpec[idx] = spec;
    info.channel[idx] = static_cast<uint8_t>(channel);
    info.dfs[idx] = dfs ? 1 : 0;
    info.passive[idx] = passive ? 1 : 0;
    info.supports40MHz[idx] = supports40MHz ? 1 : 0;
    info.supports80MHz[idx] = supports80MHz ? 1 : 0;
    info.channelBitmap[idx] = 3U | (fiveGHz ? 4U : 0U) | (dfs ? 8U : 0U) | (passive ? 32U : 0U);
    return true;
}

enum class MacUpdate {
    Invalid,
    Busy,
    Unchanged,
    Apply,
};

static inline MacUpdate planMacUpdate(const uint8_t current[6], const uint8_t *next,
                                      size_t len, bool busy) {
    if (!current || !next || len != 6 || (next[0] & 1U))
        return MacUpdate::Invalid;
    uint8_t nonZero = 0;
    for (size_t i = 0; i < 6; ++i)
        nonZero |= next[i];
    if (!nonZero)
        return MacUpdate::Invalid;
    if (memcmp(current, next, 6) == 0)
        return MacUpdate::Unchanged;
    return busy ? MacUpdate::Busy : MacUpdate::Apply;
}

// Rewrite WPA2/WPA3 transition-mode RSN IE (stripping SAE AKMs and RSNXE IE 244
// when MFPR is not mandatory) so macOS WCL negotiates WPA2-PSK with a 32-byte PMK.
static inline size_t selectWPA2TransitionMode(uint8_t *ies, size_t len) {
    if (!ies || len < 2)
        return len;

    bool strippedSae = false;
    size_t outPos = 0;
    size_t inPos = 0;

    while (inPos + 2 <= len) {
        uint8_t id = ies[inPos];
        uint8_t elen = ies[inPos + 1];
        if (inPos + 2 + elen > len)
            break;

        const uint8_t *ebody = ies + inPos + 2;
        bool rewritten = false;

        if (id == 48 && elen >= 14 && rd16(ebody) == 1) {
            size_t p = 6; // version(2) + group(4)
            if (p + 2 <= elen) {
                uint16_t pcnt = rd16(ebody + p);
                p += 2;
                if (p + static_cast<size_t>(pcnt) * 4 + 2 <= elen) {
                    size_t akmCntOfs = p + static_cast<size_t>(pcnt) * 4;
                    uint16_t acnt = rd16(ebody + akmCntOfs);
                    size_t akmListOfs = akmCntOfs + 2;
                    size_t afterAkmOfs = akmListOfs + static_cast<size_t>(acnt) * 4;
                    if (acnt >= 2 && afterAkmOfs <= elen) {
                        uint16_t rsnCap = (afterAkmOfs + 2 <= elen) ? rd16(ebody + afterAkmOfs) : 0;
                        bool mfpr = (rsnCap & 0x0040U) != 0;
                        bool hasPsk = false;
                        bool hasSae = false;
                        static constexpr uint8_t kRsnOui[3] = { 0x00, 0x0f, 0xac };
                        for (uint16_t i = 0; i < acnt; ++i) {
                            const uint8_t *s = ebody + akmListOfs + static_cast<size_t>(i) * 4;
                            if (memcmp(s, kRsnOui, 3) == 0) {
                                if (s[3] == 2 || s[3] == 6) hasPsk = true;
                                if (s[3] == 8 || s[3] == 9) hasSae = true;
                            }
                        }
                        if (hasPsk && hasSae && !mfpr) {
                            uint8_t newRsn[257] = {};
                            memcpy(newRsn, ebody, akmCntOfs);
                            uint16_t kept = 0;
                            for (uint16_t i = 0; i < acnt; ++i) {
                                const uint8_t *s = ebody + akmListOfs + static_cast<size_t>(i) * 4;
                                bool isSae = (memcmp(s, kRsnOui, 3) == 0 && (s[3] == 8 || s[3] == 9));
                                if (!isSae) {
                                    memcpy(newRsn + akmListOfs + static_cast<size_t>(kept) * 4, s, 4);
                                    ++kept;
                                }
                            }
                            newRsn[akmCntOfs] = static_cast<uint8_t>(kept & 0xffU);
                            newRsn[akmCntOfs + 1] = static_cast<uint8_t>((kept >> 8) & 0xffU);
                            size_t newBodyLen = akmListOfs + static_cast<size_t>(kept) * 4;
                            if (afterAkmOfs + 2 <= elen) {
                                uint16_t cleanCap = static_cast<uint16_t>(rsnCap & ~0x00c0U);
                                newRsn[newBodyLen++] = static_cast<uint8_t>(cleanCap & 0xffU);
                                newRsn[newBodyLen++] = static_cast<uint8_t>((cleanCap >> 8) & 0xffU);
                            }
                            ies[outPos++] = 48;
                            ies[outPos++] = static_cast<uint8_t>(newBodyLen);
                            memcpy(ies + outPos, newRsn, newBodyLen);
                            outPos += newBodyLen;
                            rewritten = true;
                            strippedSae = true;
                        }
                    }
                }
            }
        }

        if (!rewritten) {
            // Strip RSNXE (244) when downgrading transition BSS so WCL doesn't expect SAE H2E
            if (!(strippedSae && id == 244)) {
                if (outPos != inPos)
                    memmove(ies + outPos, ies + inPos, 2 + elen);
                outPos += 2 + elen;
            }
        }
        inPos += 2 + elen;
    }

    return outPos;
}

} // namespace wcl
} // namespace mt7902

#endif // AIRPORT_MT7902_WCL_HPP
