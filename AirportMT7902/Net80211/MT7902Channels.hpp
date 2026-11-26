// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// 2.4 GHz / 5 GHz / 6 GHz channel tables & Apple80211 channel flag mapping

#ifndef MT7902_CHANNELS_HPP
#define MT7902_CHANNELS_HPP

#include "../../include/MT7902Common.hpp"
#include "../HAL/MT7902McuTypes.hpp"

namespace mt7902 {

enum WifiBand : uint8_t {
    kBand2GHz = 0,
    kBand5GHz = 1,
    kBand6GHz = 2,
};

struct ChannelInfoEntry {
    uint8_t channel;
    uint16_t freqMhz;
    WifiBand band;
    bool dfs;
    bool passive;
    bool allow40;
    bool allow80;
    bool allow160;
};

// Apple80211 channel flag bits (matching apple80211_var.h)
static constexpr uint32_t kAppleChanFlag10Mhz = 0x0001;
static constexpr uint32_t kAppleChanFlag20Mhz = 0x0002;
static constexpr uint32_t kAppleChanFlag40Mhz = 0x0004;
static constexpr uint32_t kAppleChanFlag2Ghz = 0x0008;
static constexpr uint32_t kAppleChanFlag5Ghz = 0x0010;
static constexpr uint32_t kAppleChanFlagActive = 0x0020;
static constexpr uint32_t kAppleChanFlagDfs = 0x0100;
static constexpr uint32_t kAppleChanFlagExtAbv = 0x0200;
static constexpr uint32_t kAppleChanFlagExtBlw = 0x0400;
static constexpr uint32_t kAppleChanFlag80Mhz = 0x0800;
static constexpr uint32_t kAppleChanFlag160Mhz = 0x1000;
static constexpr uint32_t kAppleChanFlag6Ghz = 0x2000;

size_t getSupportedChannels(bool has2g, bool has5g, bool has6g,
                            ChannelInfoEntry *out, size_t maxOut);

const ChannelInfoEntry *findChannel(uint8_t channel, WifiBand band);
uint16_t channelToFreqMhz(uint8_t channel, WifiBand band);
uint8_t freqMhzToChannel(uint16_t freqMhz, WifiBand &outBand);
uint32_t channelToAppleFlags(const ChannelInfoEntry &ch, uint8_t bw = CMD_CBW_20MHZ);
size_t buildScanChannelList(bool has2g, bool has5g, bool has6g,
                            ScanChannelEntry *out, size_t maxOut);

} // namespace mt7902

#endif // MT7902_CHANNELS_HPP
