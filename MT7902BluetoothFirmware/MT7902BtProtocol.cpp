// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902BtProtocol.hpp"

namespace mt7902 {
namespace bt {

int MT7902BtLoader::parseFirmware79xx(const uint8_t *data, size_t size, ParsedBtFirmware &out) {
    out = {};
    if (!data || size < MTK_FW_ROM_PATCH_HEADER_SIZE + MTK_FW_ROM_PATCH_GD_SIZE)
        return kErrInvalid;

    const auto *hdr = reinterpret_cast<const BtmtkPatchHeader *>(data);
    const auto *gd = reinterpret_cast<const BtmtkGlobalDesc *>(data + MTK_FW_ROM_PATCH_HEADER_SIZE);

    uint32_t secNum = getLe32(&gd->section_num);
    if (secNum == 0 || secNum > 16)
        return kErrInvalid;

    size_t mapEnd = MTK_FW_ROM_PATCH_HEADER_SIZE + MTK_FW_ROM_PATCH_GD_SIZE +
                    static_cast<size_t>(secNum) * MTK_FW_ROM_PATCH_SEC_MAP_SIZE;
    if (size < mapEnd)
        return kErrInvalid;

    memcpy(out.buildTime, hdr->datetime, 16);
    out.buildTime[16] = '\0';
    memcpy(out.platform, hdr->platform, 4);
    out.platform[4] = '\0';
    out.hwVer = getLe16(&hdr->hwver);
    out.swVer = getLe16(&hdr->swver);
    out.sectionCount = secNum;

    for (uint32_t i = 0; i < secNum; ++i) {
        const uint8_t *secPtr = data + MTK_FW_ROM_PATCH_HEADER_SIZE +
                                MTK_FW_ROM_PATCH_GD_SIZE +
                                static_cast<size_t>(i) * MTK_FW_ROM_PATCH_SEC_MAP_SIZE;
        const auto *sec = reinterpret_cast<const BtmtkSectionMap *>(secPtr);

        uint32_t offset = getLe32(&sec->secoffset);
        uint32_t dlSize = getLe32(&sec->bin_info_spec.dlsize);
        if (dlSize > 0 && (offset > size || dlSize > size - offset))
            return kErrInvalid;

        ParsedBtSection &dst = out.sections[i];
        dst.secType = getLe32(&sec->sectype);
        dst.secOffset = offset;
        dst.dlAddr = getLe32(&sec->bin_info_spec.dlAddr);
        dst.dlSize = dlSize;
        dst.descriptor53[0] = 0; // 0 = legacy download mode (matching btmtk_setup_firmware_79xx)
        memcpy(dst.descriptor53 + 1, secPtr + MTK_SEC_MAP_COMMON_SIZE, MTK_SEC_MAP_NEED_SEND_SIZE);
    }

    return kOk;
}

size_t MT7902BtLoader::buildHciWmtCommand(uint8_t op, uint8_t flag,
                                          const void *payload, uint16_t payloadLen,
                                          uint8_t *outHciBuf, size_t outCap) {
    size_t wmtLen = sizeof(BtmtkWmtHdr) + payloadLen;
    if (wmtLen > 255 || outCap < 3 + wmtLen)
        return 0;

    // Standard 3-byte HCI command header: opcode (0xfc6f LE) + 1-byte param length
    putLe16(outHciBuf + 0, HCI_OP_MTK_WMT);
    outHciBuf[2] = static_cast<uint8_t>(wmtLen);

    auto *whdr = reinterpret_cast<BtmtkWmtHdr *>(outHciBuf + 3);
    whdr->dir = 1;
    whdr->op = op;
    putLe16(&whdr->dlen, static_cast<uint16_t>(payloadLen + 1U));
    whdr->flag = flag;

    if (payload && payloadLen > 0)
        memcpy(outHciBuf + 3 + sizeof(BtmtkWmtHdr), payload, payloadLen);

    return 3 + wmtLen;
}

int MT7902BtLoader::parseWmtEvent(uint8_t expectedOp, const uint8_t *evtBuf, size_t evtLen,
                                  WmtStatus &outStatus) {
    outStatus = BTMTK_WMT_INVALID;
    // HCI event header (2 bytes: evt=0xe4, plen) + BtmtkWmtHdr (5 bytes) = 7 bytes min
    if (!evtBuf || evtLen < 2 + sizeof(BtmtkWmtHdr))
        return kErrInvalid;

    if (evtBuf[0] != HCI_EV_WMT)
        return kErrAgain;

    const auto *whdr = reinterpret_cast<const BtmtkWmtHdr *>(evtBuf + 2);
    if (whdr->op != expectedOp)
        return kErrIO;

    switch (whdr->op) {
    case BTMTK_WMT_SEMAPHORE:
        outStatus = (whdr->flag == 2) ? BTMTK_WMT_PATCH_UNDONE : BTMTK_WMT_PATCH_DONE;
        break;
    case BTMTK_WMT_FUNC_CTRL:
        if (evtLen >= 2 + sizeof(BtmtkWmtHdr) + 2) {
            uint16_t st = getBe16(evtBuf + 2 + sizeof(BtmtkWmtHdr));
            if (st == 0x0404) outStatus = BTMTK_WMT_ON_DONE;
            else if (st == 0x0420) outStatus = BTMTK_WMT_ON_PROGRESS;
            else outStatus = BTMTK_WMT_ON_UNDONE;
        } else {
            outStatus = whdr->flag ? BTMTK_WMT_ON_UNDONE : BTMTK_WMT_ON_DONE;
        }
        break;
    case BTMTK_WMT_PATCH_DWNLD:
        if (whdr->flag == 2) outStatus = BTMTK_WMT_PATCH_DONE;
        else if (whdr->flag == 1) outStatus = BTMTK_WMT_PATCH_PROGRESS;
        else outStatus = BTMTK_WMT_PATCH_UNDONE;
        break;
    default:
        outStatus = BTMTK_WMT_ON_DONE;
        break;
    }

    return kOk;
}

int MT7902BtLoader::sendWmtSync(IBtUsbTransport &usb, uint8_t op, uint8_t flag,
                                const void *payload, uint16_t payloadLen,
                                WmtStatus *outStatus) {
    uint8_t cmdBuf[260];
    size_t cmdLen = buildHciWmtCommand(op, flag, payload, payloadLen, cmdBuf, sizeof(cmdBuf));
    if (cmdLen == 0)
        return kErrInvalid;

    int err = usb.sendHciCmdControl(cmdBuf, cmdLen);
    if (err != kOk)
        return err;

    // Poll WMT vendor event on control IN pipe (500us spacing, matching btmtk_usb_wmt_recv)
    uint8_t evtBuf[64];
    for (int attempt = 0; attempt < 2000; ++attempt) {
        size_t actual = 0;
        err = usb.pollWmtEventControl(evtBuf, sizeof(evtBuf), actual);
        if (err == kOk && actual > 0) {
            WmtStatus st = BTMTK_WMT_INVALID;
            int perr = parseWmtEvent(op, evtBuf, actual, st);
            if (perr == kErrAgain) {
                usb.delayUs(500);
                continue;
            }
            if (perr != kOk)
                return perr;
            if (outStatus)
                *outStatus = st;
            return kOk;
        }
        usb.delayUs(500);
    }

    MTK_BT_ERR("WMT op 0x%02x (flag=%u) timed out waiting for event", op, flag);
    return kErrTimeout;
}

int MT7902BtLoader::downloadFirmware79xx(IBtUsbTransport &usb, const uint8_t *data, size_t size) {
    ParsedBtFirmware parsed;
    int err = parseFirmware79xx(data, size, parsed);
    if (err != kOk) {
        MTK_BT_ERR("Invalid MT7902 BT 79xx firmware layout");
        return err;
    }

    MTK_BT_LOG("Loading MT7902 BT firmware: hw=0x%04x sw=0x%04x build=%.16s sections=%u",
               parsed.hwVer, parsed.swVer, parsed.buildTime, parsed.sectionCount);

    for (uint32_t i = 0; i < parsed.sectionCount; ++i) {
        const auto &sec = parsed.sections[i];
        if (sec.dlSize == 0)
            continue;

        WmtStatus status = BTMTK_WMT_INVALID;
        int retry = 20;
        bool alreadyDone = false;

        while (retry > 0) {
            err = sendWmtSync(usb, BTMTK_WMT_PATCH_DWNLD, 0,
                              sec.descriptor53, sizeof(sec.descriptor53), &status);
            if (err != kOk)
                return err;

            if (status == BTMTK_WMT_PATCH_UNDONE) {
                break;
            } else if (status == BTMTK_WMT_PATCH_PROGRESS) {
                usb.sleepMs(100);
                --retry;
            } else if (status == BTMTK_WMT_PATCH_DONE) {
                alreadyDone = true;
                break;
            } else {
                MTK_BT_ERR("Unexpected BT patch section %u status=%u", i, status);
                return kErrIO;
            }
        }

        if (alreadyDone)
            continue;
        if (retry == 0)
            return kErrTimeout;

        const uint8_t *ptr = data + sec.secOffset;
        uint32_t remaining = sec.dlSize;
        bool firstBlock = true;

        while (remaining > 0) {
            uint16_t chunk = (remaining > 250U) ? 250U : static_cast<uint16_t>(remaining);
            uint8_t pktFlag = BTMTK_WMT_PKT_CONTINUE;
            if (firstBlock) {
                pktFlag = BTMTK_WMT_PKT_START;
                firstBlock = false;
            } else if (remaining == chunk) {
                pktFlag = BTMTK_WMT_PKT_END;
            }

            WmtStatus chunkStatus = BTMTK_WMT_INVALID;
            err = sendWmtSync(usb, BTMTK_WMT_PATCH_DWNLD, pktFlag, ptr, chunk, &chunkStatus);
            if (err != kOk || chunkStatus == BTMTK_WMT_PATCH_PROGRESS)
                return (err != kOk) ? err : kErrIO;

            ptr += chunk;
            remaining -= chunk;
        }
    }

    // Wait 100ms for firmware activation (matching btmtk_setup_firmware_79xx)
    usb.sleepMs(100);
    return kOk;
}

int MT7902BtLoader::setupDevice(IBtUsbTransport &usb) {
    uint32_t devId = 0;
    uint32_t fwVer = 0;
    int err = usb.vendorReadReg32(MTK_BT_CHIPID_REG, devId);
    if (err != kOk) {
        MTK_BT_ERR("Failed to read MT7902 BT chip ID (%d)", err);
        return err;
    }
    (void)usb.vendorReadReg32(MTK_BT_FW_VER_REG, fwVer);

    MTK_BT_LOG("Detected MediaTek USB Bluetooth dev_id=0x%08x fw_ver=0x%08x", devId, fwVer);
    if ((devId & 0xffffU) != 0x7902U && devId != 0) {
        MTK_BT_LOG("Proceeding with 79xx BT setup on variant 0x%08x", devId);
    }

    FirmwareBuffer fwBuf;
    if (!loadEmbeddedFirmware(gBtFirmwareTable, gBtFirmwareCount, kMT7902BtFwName, fwBuf)) {
        MTK_BT_ERR("Embedded Bluetooth firmware '%s' not found", kMT7902BtFwName);
        return kErrNotFound;
    }

    err = downloadFirmware79xx(usb, fwBuf.data, fwBuf.size);
    fwBuf.release();
    if (err != kOk) {
        MTK_BT_ERR("MT7902 BT firmware download failed (%d)", err);
        return err;
    }

    // Reset USB IN/OUT endpoints (MTK_EP_RST_OPT = 0x74011890)
    err = usb.uhwWriteReg32(MTK_EP_RST_OPT, MTK_EP_RST_IN_OUT_OPT);
    if (err != kOk) {
        MTK_BT_ERR("MT7902 BT endpoint reset write failed (%d)", err);
        return err;
    }

    // Enable Bluetooth protocol via WMT_FUNC_CTRL
    uint8_t enableParam = 1;
    err = sendWmtSync(usb, BTMTK_WMT_FUNC_CTRL, 0, &enableParam, sizeof(enableParam), nullptr);
    if (err != kOk) {
        MTK_BT_ERR("MT7902 BT WMT_FUNC_CTRL enable failed (%d)", err);
        return err;
    }

    MTK_BT_LOG("MT7902 Bluetooth firmware activated successfully");
    return kOk;
}

} // namespace bt
} // namespace mt7902
