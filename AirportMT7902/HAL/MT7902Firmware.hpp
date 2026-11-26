// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Connac2 ROM Patch & WM RAM firmware parser and loader for MT7902

#ifndef MT7902_FIRMWARE_HPP
#define MT7902_FIRMWARE_HPP

#include "../../include/MT7902Common.hpp"
#include "../Firmware/MT7902FwStore.hpp"
#include "MT7902Bus.hpp"
#include "MT7902Mcu.hpp"
#include "MT7902McuTypes.hpp"

namespace mt7902 {

struct ParsedPatchSection {
    uint32_t addr = 0;
    uint32_t len = 0;
    uint32_t fileOffset = 0;
    uint32_t dlMode = 0;
};

struct ParsedPatchImage {
    char buildDate[17] = {};
    char platform[5] = {};
    uint32_t hwSwVer = 0;
    uint32_t regionCount = 0;
    ParsedPatchSection sections[16];
};

struct ParsedRamRegion {
    uint32_t addr = 0;
    uint32_t len = 0;
    uint32_t fileOffset = 0;
    uint32_t dlMode = 0;
    uint8_t featureSet = 0;
    uint8_t type = 0;
    bool nonDl = false;
    bool overrideAddr = false;
};

struct ParsedRamImage {
    uint8_t chipId = 0;
    uint8_t ecoCode = 0;
    char fwVersion[11] = {};
    char buildDate[16] = {};
    uint32_t regionCount = 0;
    uint32_t overrideEntryAddr = 0;
    ParsedRamRegion regions[16];
};

class MT7902Firmware {
public:
    MT7902Firmware(IMT7902Bus &bus, MT7902Mcu &mcu);

    // Pure parsing/validation helpers (no hardware I/O; directly unit-testable)
    static uint32_t computePatchDataMode(uint32_t secInfo);
    static uint32_t computeRamDataMode(uint8_t featureSet);
    static int parsePatchImage(const uint8_t *data, size_t size, ParsedPatchImage &out);
    static int parseRamImage(const uint8_t *data, size_t size, ParsedRamImage &out);

    // Execute full MT7902 firmware boot sequence (patch + WM RAM + ready poll)
    int loadPatch(const uint8_t *data, size_t size);
    int loadRam(const uint8_t *data, size_t size);
    int loadEmbeddedAndBoot(char outFwVer[32] = nullptr);

private:
    IMT7902Bus &mBus;
    MT7902Mcu &mMcu;
};

} // namespace mt7902

#endif // MT7902_FIRMWARE_HPP
