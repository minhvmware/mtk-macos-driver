// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902Channels.hpp"

namespace mt7902 {

static constexpr ChannelInfoEntry kChannels2G[] = {
    {  1, 2412, kBand2GHz, false, false, true, false, false },
    {  2, 2417, kBand2GHz, false, false, true, false, false },
    {  3, 2422, kBand2GHz, false, false, true, false, false },
    {  4, 2427, kBand2GHz, false, false, true, false, false },
    {  5, 2432, kBand2GHz, false, false, true, false, false },
    {  6, 2437, kBand2GHz, false, false, true, false, false },
    {  7, 2442, kBand2GHz, false, false, true, false, false },
    {  8, 2447, kBand2GHz, false, false, true, false, false },
    {  9, 2452, kBand2GHz, false, false, true, false, false },
    { 10, 2457, kBand2GHz, false, false, true, false, false },
    { 11, 2462, kBand2GHz, false, false, true, false, false },
    { 12, 2467, kBand2GHz, false, true,  true, false, false },
    { 13, 2472, kBand2GHz, false, true,  true, false, false },
};

static constexpr ChannelInfoEntry kChannels5G[] = {
    // UNII-1
    {  36, 5180, kBand5GHz, false, false, true, true, true },
    {  40, 5200, kBand5GHz, false, false, true, true, true },
    {  44, 5220, kBand5GHz, false, false, true, true, true },
    {  48, 5240, kBand5GHz, false, false, true, true, true },
    // UNII-2A (DFS)
    {  52, 5260, kBand5GHz, true,  true,  true, true, true },
    {  56, 5280, kBand5GHz, true,  true,  true, true, true },
    {  60, 5300, kBand5GHz, true,  true,  true, true, true },
    {  64, 5320, kBand5GHz, true,  true,  true, true, true },
    // UNII-2C (DFS)
    { 100, 5500, kBand5GHz, true,  true,  true, true, true },
    { 104, 5520, kBand5GHz, true,  true,  true, true, true },
    { 108, 5540, kBand5GHz, true,  true,  true, true, true },
    { 112, 5560, kBand5GHz, true,  true,  true, true, true },
    { 116, 5580, kBand5GHz, true,  true,  true, true, true },
    { 120, 5600, kBand5GHz, true,  true,  true, true, true },
    { 124, 5620, kBand5GHz, true,  true,  true, true, true },
    { 128, 5640, kBand5GHz, true,  true,  true, true, true },
    { 132, 5660, kBand5GHz, true,  true,  true, true, false },
    { 136, 5680, kBand5GHz, true,  true,  true, true, false },
    { 140, 5700, kBand5GHz, true,  true,  true, true, false },
    { 144, 5720, kBand5GHz, true,  true,  true, true, false },
    // UNII-3
    { 149, 5745, kBand5GHz, false, false, true, true, false },
    { 153, 5765, kBand5GHz, false, false, true, true, false },
    { 157, 5785, kBand5GHz, false, false, true, true, false },
    { 161, 5805, kBand5GHz, false, false, true, true, false },
    { 165, 5825, kBand5GHz, false, false, false, false, false },
};

// Preferred Scanning Channels (PSC) for 6 GHz Wi-Fi 6E
static constexpr ChannelInfoEntry kChannels6G[] = {
    {   5, 5975, kBand6GHz, false, false, true, true, true },
    {  21, 6055, kBand6GHz, false, false, true, true, true },
    {  37, 6135, kBand6GHz, false, false, true, true, true },
    {  53, 6215, kBand6GHz, false, false, true, true, true },
    {  69, 6295, kBand6GHz, false, false, true, true, true },
    {  85, 6375, kBand6GHz, false, false, true, true, true },
    { 101, 6455, kBand6GHz, false, false, true, true, true },
    { 117, 6535, kBand6GHz, false, false, true, true, true },
    { 133, 6615, kBand6GHz, false, false, true, true, true },
    { 149, 6695, kBand6GHz, false, false, true, true, true },
    { 165, 6775, kBand6GHz, false, false, true, true, true },
    { 181, 6855, kBand6GHz, false, false, true, true, true },
    { 197, 6935, kBand6GHz, false, false, true, true, true },
    { 213, 7015, kBand6GHz, false, false, true, true, true },
};

size_t getSupportedChannels(bool has2g, bool has5g, bool has6g,
                            ChannelInfoEntry *out, size_t maxOut) {
    size_t n = 0;
    if (has2g) {
        for (const auto &c : kChannels2G) {
            if (n < maxOut) out[n++] = c;
        }
    }
    if (has5g) {
        for (const auto &c : kChannels5G) {
            if (n < maxOut) out[n++] = c;
        }
    }
    if (has6g) {
        for (const auto &c : kChannels6G) {
            if (n < maxOut) out[n++] = c;
        }
    }
    return n;
}

const ChannelInfoEntry *findChannel(uint8_t channel, WifiBand band) {
    if (band == kBand2GHz) {
        for (const auto &c : kChannels2G)
            if (c.channel == channel) return &c;
    } else if (band == kBand5GHz) {
        for (const auto &c : kChannels5G)
            if (c.channel == channel) return &c;
    } else {
        for (const auto &c : kChannels6G)
            if (c.channel == channel) return &c;
    }
    return nullptr;
}

uint16_t channelToFreqMhz(uint8_t channel, WifiBand band) {
    if (band == kBand2GHz) {
        if (channel == 14) return 2484;
        if (channel >= 1 && channel <= 13) return static_cast<uint16_t>(2407 + channel * 5);
    } else if (band == kBand5GHz) {
        return static_cast<uint16_t>(5000 + channel * 5);
    } else {
        return static_cast<uint16_t>(5950 + channel * 5);
    }
    return 2412;
}

uint8_t freqMhzToChannel(uint16_t freqMhz, WifiBand &outBand) {
    if (freqMhz == 2484) {
        outBand = kBand2GHz;
        return 14;
    }
    if (freqMhz >= 2412 && freqMhz <= 2472) {
        outBand = kBand2GHz;
        return static_cast<uint8_t>((freqMhz - 2407) / 5);
    }
    if (freqMhz >= 5180 && freqMhz <= 5885) {
        outBand = kBand5GHz;
        return static_cast<uint8_t>((freqMhz - 5000) / 5);
    }
    if (freqMhz >= 5955 && freqMhz <= 7115) {
        outBand = kBand6GHz;
        return static_cast<uint8_t>((freqMhz - 5950) / 5);
    }
    outBand = kBand2GHz;
    return 1;
}

uint32_t channelToAppleFlags(const ChannelInfoEntry &ch, uint8_t bw) {
    uint32_t f = 0;
    if (ch.band == kBand2GHz) f |= kAppleChanFlag2Ghz;
    else if (ch.band == kBand5GHz) f |= kAppleChanFlag5Ghz;
    else f |= kAppleChanFlag6Ghz;

    if (!ch.passive) f |= kAppleChanFlagActive;
    if (ch.dfs) f |= kAppleChanFlagDfs;

    switch (bw) {
    case CMD_CBW_160MHZ: f |= kAppleChanFlag160Mhz; break;
    case CMD_CBW_80MHZ:  f |= kAppleChanFlag80Mhz;  break;
    case CMD_CBW_40MHZ:  f |= kAppleChanFlag40Mhz;  break;
    default:             f |= kAppleChanFlag20Mhz;  break;
    }
    return f;
}

size_t buildScanChannelList(bool has2g, bool has5g, bool has6g,
                            ScanChannelEntry *out, size_t maxOut) {
    size_t n = 0;
    if (has2g) {
        for (const auto &c : kChannels2G) {
            if (n < maxOut) out[n++] = { 1, c.channel };
        }
    }
    if (has5g) {
        for (const auto &c : kChannels5G) {
            if (n < maxOut) out[n++] = { 2, c.channel };
        }
    }
    if (has6g) {
        for (const auto &c : kChannels6G) {
            if (n < maxOut) out[n++] = { 3, c.channel };
        }
    }
    return n;
}

} // namespace mt7902
