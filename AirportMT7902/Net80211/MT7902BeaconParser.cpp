// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902BeaconParser.hpp"

namespace mt7902 {

MT7902ScanCache::MT7902ScanCache() {
    clear();
}

void MT7902ScanCache::clear() {
    for (size_t i = 0; i < kMaxScanCacheEntries; ++i)
        mEntries[i].valid = false;
    mCursor = 0;
}

static void parseRsnIe(const uint8_t *body, size_t len, ScannedBss &out) {
    if (len < 8)
        return;
    // Version (2) + Group Cipher (4) + Pairwise Count (2)
    uint16_t ver = getLe16(body);
    if (ver != 1)
        return;
    out.hasRsn = true;
    size_t pos = 6;
    uint16_t pairwiseCnt = getLe16(body + pos);
    pos += 2;
    if (pos + static_cast<size_t>(pairwiseCnt) * 4 + 2 > len)
        return;
    pos += static_cast<size_t>(pairwiseCnt) * 4;

    uint16_t akmCnt = getLe16(body + pos);
    pos += 2;
    for (uint16_t i = 0; i < akmCnt && pos + 4 <= len; ++i, pos += 4) {
        if (body[pos] == 0x00 && body[pos + 1] == 0x0f && body[pos + 2] == 0xac) {
            uint8_t akm = body[pos + 3];
            if (akm == 1 || akm == 5 || akm == 3)
                out.has8021x = true;
            else if (akm == 2 || akm == 6 || akm == 4)
                out.hasPsk = true;
            else if (akm == 8 || akm == 9 || akm == 24)
                out.hasSae = true;
        }
    }
}

bool MT7902ScanCache::parseBeaconOrProbeResp(const uint8_t *frame, size_t len,
                                             int32_t rssi, uint8_t rxChannel,
                                             uint64_t nowMs, ScannedBss &out) {
    // 24-byte 802.11 mgmt header + 12-byte fixed fields (timestamp[8], interval[2], capab[2])
    if (!frame || len < 36)
        return false;

    uint16_t fc = getLe16(frame);
    uint8_t type = (fc >> 2) & 0x3U;
    uint8_t stype = (fc >> 4) & 0xfU;
    if (type != 0 || (stype != 8 && stype != 5)) // Beacon=8, ProbeResp=5
        return false;

    out = {};
    out.valid = true;
    memcpy(out.bssid, frame + 16, 6);
    out.beaconInterval = getLe16(frame + 24 + 8);
    out.capability = getLe16(frame + 24 + 10);
    out.rssi = rssi;
    out.noise = -95;
    out.channel = rxChannel ? rxChannel : 1;
    out.band = (out.channel > 14) ? kBand5GHz : kBand2GHz;
    out.lastSeenMs = nowMs;

    const uint8_t *ies = frame + 36;
    size_t ieTotal = len - 36;
    if (ieTotal > kMaxRawIeLen)
        ieTotal = kMaxRawIeLen;
    memcpy(out.ies, ies, ieTotal);
    out.ieLen = static_cast<uint16_t>(ieTotal);

    size_t rawCopy = len > sizeof(out.rawFrame) ? sizeof(out.rawFrame) : len;
    memcpy(out.rawFrame, frame, rawCopy);
    out.rawFrameLen = static_cast<uint16_t>(rawCopy);

    size_t pos = 0;
    while (pos + 2 <= ieTotal) {
        uint8_t id = ies[pos];
        uint8_t elen = ies[pos + 1];
        if (pos + 2 + elen > ieTotal)
            break;
        const uint8_t *body = ies + pos + 2;

        switch (id) {
        case 0: // SSID
            if (elen <= 32) {
                memcpy(out.ssid, body, elen);
                out.ssidLen = elen;
            }
            break;
        case 3: // DS Parameter Set (channel)
            if (elen >= 1 && body[0] != 0) {
                out.channel = body[0];
                out.band = (out.channel > 14) ? kBand5GHz : kBand2GHz;
            }
            break;
        case 5: // TIM
            if (elen >= 2 && body[1] > 0)
                out.dtimPeriod = body[1];
            break;
        case 7: // Country
            if (elen >= 2) {
                out.countryCode[0] = static_cast<char>(body[0]);
                out.countryCode[1] = static_cast<char>(body[1]);
                out.countryCode[2] = '\0';
            }
            break;
        case 45: // HT Capabilities
            out.hasHt = true;
            if (elen >= 2 && (getLe16(body) & MTK_BIT(1)))
                out.bw = CMD_CBW_40MHZ;
            break;
        case 48: // RSN (WPA2/WPA3)
            parseRsnIe(body, elen, out);
            break;
        case 61: // HT Operation
            if (elen >= 1 && body[0] != 0) {
                out.channel = body[0];
                out.band = (out.channel > 14) ? kBand5GHz : kBand2GHz;
            }
            break;
        case 191: // VHT Capabilities
            out.hasVht = true;
            if (out.bw < CMD_CBW_80MHZ)
                out.bw = CMD_CBW_80MHZ;
            break;
        case 192: // VHT Operation
            if (elen >= 1 && body[0] >= 1)
                out.bw = (body[0] == 2) ? CMD_CBW_160MHZ : CMD_CBW_80MHZ;
            break;
        case 221: // Vendor Specific (WPA1 / WMM)
            if (elen >= 4 && body[0] == 0x00 && body[1] == 0x50 && body[2] == 0xf2) {
                if (body[3] == 0x01) {
                    out.hasWpa = true;
                    out.hasPsk = true;
                } else if (body[3] == 0x02) {
                    out.hasQos = true;
                }
            }
            break;
        case 255: // Extension IE (HE Capabilities = ext 35, HE Operation = ext 36)
            if (elen >= 1 && (body[0] == 35 || body[0] == 36))
                out.hasHe = true;
            break;
        default:
            break;
        }
        pos += 2 + elen;
    }

    return true;
}

void MT7902ScanCache::upsert(const ScannedBss &bss) {
    if (!bss.valid)
        return;

    // Update existing BSSID entry if present
    size_t freeIdx = kMaxScanCacheEntries;
    size_t oldestIdx = 0;
    uint64_t oldestTime = ~0ULL;

    for (size_t i = 0; i < kMaxScanCacheEntries; ++i) {
        if (mEntries[i].valid) {
            if (memcmp(mEntries[i].bssid, bss.bssid, 6) == 0) {
                // Preserve non-empty SSID if beacon was hidden and probe resp populated it
                ScannedBss merged = bss;
                if (merged.ssidLen == 0 && mEntries[i].ssidLen > 0) {
                    memcpy(merged.ssid, mEntries[i].ssid, mEntries[i].ssidLen);
                    merged.ssidLen = mEntries[i].ssidLen;
                }
                mEntries[i] = merged;
                return;
            }
            if (mEntries[i].lastSeenMs < oldestTime) {
                oldestTime = mEntries[i].lastSeenMs;
                oldestIdx = i;
            }
        } else if (freeIdx == kMaxScanCacheEntries) {
            freeIdx = i;
        }
    }

    size_t target = (freeIdx < kMaxScanCacheEntries) ? freeIdx : oldestIdx;
    mEntries[target] = bss;
}

void MT7902ScanCache::pruneOlderThan(uint64_t nowMs, uint64_t maxAgeMs) {
    for (size_t i = 0; i < kMaxScanCacheEntries; ++i) {
        if (mEntries[i].valid && nowMs > mEntries[i].lastSeenMs &&
            (nowMs - mEntries[i].lastSeenMs) > maxAgeMs) {
            mEntries[i].valid = false;
        }
    }
}

size_t MT7902ScanCache::count() const {
    size_t c = 0;
    for (size_t i = 0; i < kMaxScanCacheEntries; ++i)
        if (mEntries[i].valid) ++c;
    return c;
}

const ScannedBss *MT7902ScanCache::at(size_t slot) const {
    if (slot >= kMaxScanCacheEntries || !mEntries[slot].valid)
        return nullptr;
    return &mEntries[slot];
}

const ScannedBss *MT7902ScanCache::findByBssid(const uint8_t bssid[6]) const {
    for (size_t i = 0; i < kMaxScanCacheEntries; ++i) {
        if (mEntries[i].valid && memcmp(mEntries[i].bssid, bssid, 6) == 0)
            return &mEntries[i];
    }
    return nullptr;
}

const ScannedBss *MT7902ScanCache::findBySsid(const uint8_t *ssid, size_t ssidLen) const {
    const ScannedBss *best = nullptr;
    for (size_t i = 0; i < kMaxScanCacheEntries; ++i) {
        if (!mEntries[i].valid || mEntries[i].ssidLen != ssidLen)
            continue;
        if (ssidLen == 0 || memcmp(mEntries[i].ssid, ssid, ssidLen) == 0) {
            if (!best || mEntries[i].rssi > best->rssi)
                best = &mEntries[i];
        }
    }
    return best;
}

const ScannedBss *MT7902ScanCache::nextResult() {
    while (mCursor < kMaxScanCacheEntries) {
        const ScannedBss *entry = &mEntries[mCursor++];
        if (entry->valid)
            return entry;
    }
    return nullptr;
}

} // namespace mt7902
