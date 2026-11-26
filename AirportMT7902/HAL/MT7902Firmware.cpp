// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902Firmware.hpp"
#include "MT7902Regs.hpp"

namespace mt7902 {

MT7902Firmware::MT7902Firmware(IMT7902Bus &bus, MT7902Mcu &mcu)
    : mBus(bus), mMcu(mcu) {}

uint32_t MT7902Firmware::computePatchDataMode(uint32_t secInfo) {
    uint32_t mode = DL_MODE_NEED_RSP;
    if (secInfo == PATCH_SEC_NOT_SUPPORT)
        return mode;

    switch (fieldGet(PATCH_SEC_ENC_TYPE_MASK, secInfo)) {
    case PATCH_SEC_ENC_TYPE_PLAIN:
        break;
    case PATCH_SEC_ENC_TYPE_AES:
        mode |= DL_MODE_ENCRYPT;
        mode |= fieldPrep(DL_MODE_KEY_IDX, secInfo & PATCH_SEC_ENC_AES_KEY_MASK);
        mode |= DL_MODE_RESET_SEC_IV;
        break;
    case PATCH_SEC_ENC_TYPE_SCRAMBLE:
        mode |= DL_MODE_ENCRYPT | DL_CONFIG_ENCRY_MODE_SEL | DL_MODE_RESET_SEC_IV;
        break;
    default:
        MTK_ERR("Unsupported MT7902 patch encryption type 0x%x",
                fieldGet(PATCH_SEC_ENC_TYPE_MASK, secInfo));
        break;
    }
    return mode;
}

uint32_t MT7902Firmware::computeRamDataMode(uint8_t featureSet) {
    uint32_t mode = DL_MODE_NEED_RSP;
    if (featureSet & FW_FEATURE_SET_ENCRYPT) {
        mode |= DL_MODE_ENCRYPT | DL_MODE_RESET_SEC_IV;
        mode |= fieldPrep(DL_MODE_KEY_IDX, fieldGet(FW_FEATURE_SET_KEY_IDX, featureSet));
        if (featureSet & FW_FEATURE_ENCRY_MODE)
            mode |= DL_CONFIG_ENCRY_MODE_SEL;
    }
    return mode;
}

int MT7902Firmware::parsePatchImage(const uint8_t *data, size_t size, ParsedPatchImage &out) {
    out = {};
    if (!data || size < sizeof(Mt76Connac2PatchHdr))
        return kErrInvalid;

    const auto *hdr = reinterpret_cast<const Mt76Connac2PatchHdr *>(data);
    memcpy(out.buildDate, hdr->build_date, 16);
    out.buildDate[16] = '\0';
    for (int k = 15; k >= 0; --k) {
        if (out.buildDate[k] == '\n' || out.buildDate[k] == '\r' || out.buildDate[k] == ' ')
            out.buildDate[k] = '\0';
        else if (out.buildDate[k] != '\0')
            break;
    }
    memcpy(out.platform, hdr->platform, 4);
    out.platform[4] = '\0';
    out.hwSwVer = getBe32(&hdr->hw_sw_ver);
    uint32_t nRegion = getBe32(&hdr->desc.n_region);

    if (nRegion == 0 || nRegion > 16)
        return kErrInvalid;
    if (size < sizeof(Mt76Connac2PatchHdr) + static_cast<size_t>(nRegion) * sizeof(Mt76Connac2PatchSec))
        return kErrInvalid;

    out.regionCount = nRegion;
    for (uint32_t i = 0; i < nRegion; ++i) {
        const auto *sec = reinterpret_cast<const Mt76Connac2PatchSec *>(
            data + sizeof(Mt76Connac2PatchHdr) + i * sizeof(Mt76Connac2PatchSec));

        uint32_t secType = getBe32(&sec->type);
        if ((secType & PATCH_SEC_TYPE_MASK) != PATCH_SEC_TYPE_INFO)
            return kErrInvalid;

        uint32_t offs = getBe32(&sec->offs);
        uint32_t addr = getBe32(&sec->info.addr);
        uint32_t len = getBe32(&sec->info.len);
        uint32_t keyInfo = getBe32(&sec->info.sec_key_idx);

        if (offs > size || len > size - offs)
            return kErrInvalid;

        out.sections[i].addr = addr;
        out.sections[i].len = len;
        out.sections[i].fileOffset = offs;
        out.sections[i].dlMode = computePatchDataMode(keyInfo);
    }

    return kOk;
}

int MT7902Firmware::parseRamImage(const uint8_t *data, size_t size, ParsedRamImage &out) {
    out = {};
    if (!data || size < sizeof(Mt76Connac2FwTrailer))
        return kErrInvalid;

    size_t trailerOff = size - sizeof(Mt76Connac2FwTrailer);
    const auto *hdr = reinterpret_cast<const Mt76Connac2FwTrailer *>(data + trailerOff);
    if (hdr->n_region == 0 || hdr->n_region > 16)
        return kErrInvalid;

    size_t regionsBytes = static_cast<size_t>(hdr->n_region) * sizeof(Mt76Connac2FwRegion);
    if (trailerOff < regionsBytes)
        return kErrInvalid;

    out.chipId = hdr->chip_id;
    out.ecoCode = hdr->eco_code;
    out.regionCount = hdr->n_region;
    memcpy(out.fwVersion, hdr->fw_ver, 10);
    out.fwVersion[10] = '\0';
    memcpy(out.buildDate, hdr->build_date, 15);
    out.buildDate[15] = '\0';

    size_t dataOffset = 0;
    size_t maxPayload = trailerOff - regionsBytes;

    for (uint32_t i = 0; i < hdr->n_region; ++i) {
        const auto *reg = reinterpret_cast<const Mt76Connac2FwRegion *>(
            data + trailerOff - (hdr->n_region - i) * sizeof(Mt76Connac2FwRegion));

        uint32_t rlen = getLe32(&reg->len);
        uint32_t addr = getLe32(&reg->addr);
        if (dataOffset > maxPayload || rlen > maxPayload - dataOffset)
            return kErrInvalid;

        ParsedRamRegion &dst = out.regions[i];
        dst.addr = addr;
        dst.len = rlen;
        dst.fileOffset = static_cast<uint32_t>(dataOffset);
        dst.featureSet = reg->feature_set;
        dst.type = reg->type;
        dst.nonDl = (reg->feature_set & FW_FEATURE_NON_DL) != 0;
        dst.overrideAddr = (reg->feature_set & FW_FEATURE_OVERRIDE_ADDR) != 0;
        dst.dlMode = computeRamDataMode(reg->feature_set);

        if (!dst.nonDl && dst.overrideAddr)
            out.overrideEntryAddr = addr;

        dataOffset += rlen;
    }

    return kOk;
}

int MT7902Firmware::loadPatch(const uint8_t *data, size_t size) {
    ParsedPatchImage img;
    int err = parsePatchImage(data, size, img);
    if (err != kOk) {
        MTK_ERR("Invalid MT7902 ROM patch image");
        return err;
    }

    uint8_t semStatus = 0;
    err = mMcu.patchSemCtrl(true, semStatus);
    if (err != kOk)
        return err;

    if (semStatus == PATCH_IS_DL) {
        MTK_LOG("MT7902 ROM patch already loaded");
        return kOk;
    }
    if (semStatus != PATCH_NOT_DL_SEM_SUCCESS) {
        MTK_ERR("Failed to acquire MT7902 patch semaphore (status=%u)", semStatus);
        return kErrAgain;
    }

    MTK_LOG("Loading MT7902 ROM patch: hw_sw=0x%08x build=%.16s regions=%u",
            img.hwSwVer, img.buildDate, img.regionCount);

    for (uint32_t i = 0; i < img.regionCount; ++i) {
        const auto &sec = img.sections[i];
        err = mMcu.initDownload(sec.addr, sec.len, sec.dlMode);
        if (err != kOk) {
            MTK_ERR("Patch section %u initDownload failed (%d)", i, err);
            goto release_sem;
        }
        err = mMcu.sendFirmwareScatter(data + sec.fileOffset, sec.len, 4096);
        if (err != kOk) {
            MTK_ERR("Patch section %u scatter failed (%d)", i, err);
            goto release_sem;
        }
    }

    {
        uint8_t finishStatus = 0;
        err = mMcu.startPatch(finishStatus);
        if (err != kOk)
            MTK_ERR("Patch finish request failed (%d)", err);
    }

release_sem:
    uint8_t relStatus = 0;
    int relErr = mMcu.patchSemCtrl(false, relStatus);
    if (relErr != kOk || relStatus != PATCH_REL_SEM_SUCCESS) {
        MTK_ERR("Failed to release patch semaphore (err=%d status=%u)", relErr, relStatus);
        if (err == kOk)
            err = kErrAgain;
    }
    return err;
}

int MT7902Firmware::loadRam(const uint8_t *data, size_t size) {
    ParsedRamImage img;
    int err = parseRamImage(data, size, img);
    if (err != kOk) {
        MTK_ERR("Invalid MT7902 WM RAM firmware image");
        return err;
    }

    MTK_LOG("Loading MT7902 WM RAM: ver=%.10s build=%.15s regions=%u",
            img.fwVersion, img.buildDate, img.regionCount);

    for (uint32_t i = 0; i < img.regionCount; ++i) {
        const auto &reg = img.regions[i];
        if (reg.nonDl)
            continue; // Skip NON_DL sections such as CLC regulatory tables

        err = mMcu.initDownload(reg.addr, reg.len, reg.dlMode);
        if (err != kOk) {
            MTK_ERR("RAM region %u initDownload failed (%d)", i, err);
            return err;
        }
        err = mMcu.sendFirmwareScatter(data + reg.fileOffset, reg.len, 4096);
        if (err != kOk) {
            MTK_ERR("RAM region %u scatter failed (%d)", i, err);
            return err;
        }
    }

    uint32_t option = img.overrideEntryAddr ? FW_START_OVERRIDE : 0;
    err = mMcu.startFirmware(img.overrideEntryAddr, option);
    if (err != kOk) {
        MTK_ERR("WM firmware start request failed (%d)", err);
        return err;
    }

    return kOk;
}

int MT7902Firmware::loadEmbeddedAndBoot(char outFwVer[32]) {
    FirmwareBuffer patchBuf;
    FirmwareBuffer ramBuf;

    if (!loadEmbeddedFirmware(gWifiFirmwareTable, gWifiFirmwareCount, kMT7902PatchFwName, patchBuf)) {
        MTK_ERR("Missing embedded Wi-Fi patch firmware: %s", kMT7902PatchFwName);
        return kErrNotFound;
    }
    if (!loadEmbeddedFirmware(gWifiFirmwareTable, gWifiFirmwareCount, kMT7902RamFwName, ramBuf)) {
        patchBuf.release();
        MTK_ERR("Missing embedded Wi-Fi RAM firmware: %s", kMT7902RamFwName);
        return kErrNotFound;
    }

    // Put firmware into normal boot state
    mBus.wr(MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
    (void)mMcu.restartFirmware();

    if (!mBus.pollMs(MT_CONN_ON_MISC, MT_TOP_MISC_FW_STATE, MT_TOP_MISC2_FW_PWR_ON, 1000, 1)) {
        MTK_LOG("Warning: MT_CONN_ON_MISC not in FW_PWR_ON before patch download (val=0x%08x)",
                mBus.rr(MT_CONN_ON_MISC));
    }

    int err = loadPatch(patchBuf.data, patchBuf.size);
    if (err != kOk)
        goto out;

    err = loadRam(ramBuf.data, ramBuf.size);
    if (err != kOk)
        goto out;

    if (!mBus.pollMs(MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY, MT_TOP_MISC2_FW_N9_RDY, 1500, 1)) {
        MTK_ERR("Timeout waiting for MT7902 N9 firmware ready (MT_CONN_ON_MISC=0x%08x)",
                mBus.rr(MT_CONN_ON_MISC));
        err = kErrIO;
        goto out;
    }

    if (outFwVer) {
        ParsedRamImage parsed;
        if (parseRamImage(ramBuf.data, ramBuf.size, parsed) == kOk) {
            snprintf(outFwVer, 32, "%.10s-%.15s", parsed.fwVersion, parsed.buildDate);
        }
    }

    MTK_LOG("MT7902 Wi-Fi firmware booted successfully");

out:
    patchBuf.release();
    ramBuf.release();
    return err;
}

} // namespace mt7902
