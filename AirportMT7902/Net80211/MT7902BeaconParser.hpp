// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// 802.11 Beacon / Probe Response IE parser & scan cache

#ifndef MT7902_BEACON_PARSER_HPP
#define MT7902_BEACON_PARSER_HPP

#include "../../include/MT7902Common.hpp"
#include "MT7902Channels.hpp"

namespace mt7902 {

static constexpr size_t kMaxRawIeLen = 1024;
static constexpr size_t kMaxScanCacheEntries = 128;

struct ScannedBss {
    bool valid = false;
    uint8_t bssid[6] = {};
    uint8_t ssid[32] = {};
    uint8_t ssidLen = 0;
    uint8_t channel = 1;
    uint8_t centerChannel = 0;
    WifiBand band = kBand2GHz;
    uint8_t bw = CMD_CBW_20MHZ;
    int32_t rssi = -70;
    int32_t noise = -95;
    uint16_t beaconInterval = 100;
    uint16_t capability = 0;
    uint8_t dtimPeriod = 1;
    uint64_t lastSeenMs = 0;

    // Parsed capabilities
    bool hasWpa = false;
    bool hasRsn = false;
    bool hasPsk = false;
    bool hasSae = false;
    bool has8021x = false;
    bool hasHt = false;
    bool hasVht = false;
    bool hasHe = false;
    uint8_t bssColor = 0;
    bool hasQos = false;
    char countryCode[3] = {};

    // Raw IE payload (bytes immediately following 12-byte fixed beacon fields)
    uint16_t ieLen = 0;
    uint8_t ies[kMaxRawIeLen] = {};

    // Full raw 802.11 Beacon / Probe Response frame (up to 768 bytes) for WCL BEACON_INFO TSF tracking
    uint16_t rawFrameLen = 0;
    uint8_t rawFrame[768] = {};
};

class MT7902ScanCache {
public:
    MT7902ScanCache();

    // Parse raw 802.11 Beacon (0x80) or Probe Response (0x50) frame
    static bool parseBeaconOrProbeResp(const uint8_t *frame, size_t len,
                                       int32_t rssi, uint8_t rxChannel,
                                       uint64_t nowMs, ScannedBss &out);

    void clear();
    void upsert(const ScannedBss &bss);
    void pruneOlderThan(uint64_t nowMs, uint64_t maxAgeMs);

    size_t count() const;
    const ScannedBss *at(size_t slot) const;
    const ScannedBss *findByBssid(const uint8_t bssid[6]) const;
    const ScannedBss *findBySsid(const uint8_t *ssid, size_t ssidLen) const;

    // Cursor iteration for macOS APPLE80211_IOC_SCAN_RESULT
    void resetCursor() { mCursor = 0; }
    const ScannedBss *nextResult();

private:
    ScannedBss mEntries[kMaxScanCacheEntries];
    size_t mCursor = 0;
};

} // namespace mt7902

#endif // MT7902_BEACON_PARSER_HPP
