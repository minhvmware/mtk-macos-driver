// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// MediaTek 79xx Bluetooth USB WMT protocol & firmware loader (derived from Linux btmtk.c/h)

#ifndef MT7902_BT_PROTOCOL_HPP
#define MT7902_BT_PROTOCOL_HPP

#include "../include/MT7902Common.hpp"
#include "../AirportMT7902/Firmware/MT7902FwStore.hpp"

namespace mt7902 {
namespace bt {

static constexpr const char *kMT7902BtFwName = "BT_RAM_CODE_MT7902_1_1_hdr.bin";

static constexpr size_t MTK_FW_ROM_PATCH_HEADER_SIZE = 32;
static constexpr size_t MTK_FW_ROM_PATCH_GD_SIZE = 64;
static constexpr size_t MTK_FW_ROM_PATCH_SEC_MAP_SIZE = 64;
static constexpr size_t MTK_SEC_MAP_COMMON_SIZE = 12;
static constexpr size_t MTK_SEC_MAP_NEED_SEND_SIZE = 52;

static constexpr uint8_t HCI_EV_WMT = 0xe4;
static constexpr uint16_t HCI_OP_MTK_WMT = 0xfc6f;

static constexpr uint32_t MTK_BT_CHIPID_REG = 0x70010200;
static constexpr uint32_t MTK_BT_FW_VER_REG = 0x80021004;
static constexpr uint32_t MTK_BT_FLAVOR_REG = 0x70010020;
static constexpr uint32_t MTK_EP_RST_OPT = 0x74011890;
static constexpr uint32_t MTK_EP_RST_IN_OUT_OPT = 0x00010001;

enum WmtOp : uint8_t {
    BTMTK_WMT_PATCH_DWNLD = 0x01,
    BTMTK_WMT_FUNC_CTRL = 0x06,
    BTMTK_WMT_RST = 0x07,
    BTMTK_WMT_SEMAPHORE = 0x17,
};

enum WmtStatus : uint8_t {
    BTMTK_WMT_INVALID = 0,
    BTMTK_WMT_PATCH_UNDONE = 1,
    BTMTK_WMT_PATCH_PROGRESS = 2,
    BTMTK_WMT_PATCH_DONE = 3,
    BTMTK_WMT_ON_UNDONE = 4,
    BTMTK_WMT_ON_DONE = 5,
    BTMTK_WMT_ON_PROGRESS = 6,
};

enum WmtPktFlag : uint8_t {
    BTMTK_WMT_PKT_START = 1,
    BTMTK_WMT_PKT_CONTINUE = 2,
    BTMTK_WMT_PKT_END = 3,
};

struct __attribute__((packed)) BtmtkPatchHeader {
    uint8_t datetime[16];
    uint8_t platform[4];
    uint16_t hwver;
    uint16_t swver;
    uint32_t magicnum;
    uint16_t rsv[2]; // Total header = 32 bytes
};
static_assert(sizeof(BtmtkPatchHeader) == 32, "BT patch header must be 32 bytes");

struct __attribute__((packed)) BtmtkGlobalDesc {
    uint32_t patch_ver;
    uint32_t sub_sys;
    uint32_t feature_opt;
    uint32_t section_num;
    uint8_t rsv[48]; // Total global descriptor = 64 bytes
};
static_assert(sizeof(BtmtkGlobalDesc) == 64, "BT global desc must be 64 bytes");

struct __attribute__((packed)) BtmtkSectionMap {
    uint32_t sectype;
    uint32_t secoffset;
    uint32_t secsize;
    union {
        uint32_t u4SecSpec[13];
        struct {
            uint32_t dlAddr;
            uint32_t dlsize;
            uint32_t seckeyidx;
            uint32_t alignlen;
            uint32_t sectype_inner;
            uint32_t dlmodecrctype;
            uint32_t crc;
            uint32_t reserved[6];
        } bin_info_spec;
    };
};
static_assert(sizeof(BtmtkSectionMap) == 64, "BT section map must be 64 bytes");

struct __attribute__((packed)) BtmtkWmtHdr {
    uint8_t dir;
    uint8_t op;
    uint16_t dlen;
    uint8_t flag;
};
static_assert(sizeof(BtmtkWmtHdr) == 5, "WMT header must be 5 bytes");

struct ParsedBtSection {
    uint32_t secType = 0;
    uint32_t secOffset = 0;
    uint32_t dlAddr = 0;
    uint32_t dlSize = 0;
    uint8_t descriptor53[MTK_SEC_MAP_NEED_SEND_SIZE + 1] = {};
};

struct ParsedBtFirmware {
    char buildTime[17] = {};
    char platform[5] = {};
    uint16_t hwVer = 0;
    uint16_t swVer = 0;
    uint32_t sectionCount = 0;
    ParsedBtSection sections[16];
};

// Abstract USB control-pipe interface implemented by IOUSBHostDevice on macOS (and mock in tests)
class IBtUsbTransport {
public:
    virtual ~IBtUsbTransport() = default;

    // USB Vendor IN (bmRequestType=0xC0, bRequest=0x63, wValue=reg>>16, wIndex=reg&0xffff, len=4)
    virtual int vendorReadReg32(uint32_t reg, uint32_t &outVal) = 0;

    // USB Vendor OUT UHW write (bmRequestType=0x5E, bRequest=0x02, wValue=reg>>16, wIndex=reg&0xffff, len=4)
    virtual int uhwWriteReg32(uint32_t reg, uint32_t val) = 0;

    // USB Class Control OUT HCI command (bmRequestType=0x20, bRequest=0x00, wValue=0, wIndex=0)
    virtual int sendHciCmdControl(const uint8_t *hciCmdPkt, size_t len) = 0;

    // USB Vendor IN WMT event poll (bmRequestType=0xC0, bRequest=0x01, wValue=48, wIndex=0, max 64B)
    virtual int pollWmtEventControl(uint8_t *outBuf, size_t maxLen, size_t &actualLen) = 0;

    virtual void delayUs(uint32_t us) = 0;
    virtual void sleepMs(uint32_t ms) = 0;
};

class MT7902BtLoader {
public:
    // Pure layout validation & packet builders (directly unit-testable)
    static int parseFirmware79xx(const uint8_t *data, size_t size, ParsedBtFirmware &out);
    static size_t buildHciWmtCommand(uint8_t op, uint8_t flag,
                                     const void *payload, uint16_t payloadLen,
                                     uint8_t *outHciBuf, size_t outCap);
    static int parseWmtEvent(uint8_t expectedOp, const uint8_t *evtBuf, size_t evtLen,
                             WmtStatus &outStatus);

    // Synchronous WMT command/event transaction over USB control pipe
    static int sendWmtSync(IBtUsbTransport &usb, uint8_t op, uint8_t flag,
                           const void *payload, uint16_t payloadLen,
                           WmtStatus *outStatus = nullptr);

    // Download 79xx sectioned RAM firmware (matches btmtk_setup_firmware_79xx)
    static int downloadFirmware79xx(IBtUsbTransport &usb, const uint8_t *data, size_t size);

    // Full MT7902 USB Bluetooth setup sequence (chip check + patch download + EP reset + FUNC_CTRL)
    static int setupDevice(IBtUsbTransport &usb);
};

} // namespace bt
} // namespace mt7902

#endif // MT7902_BT_PROTOCOL_HPP
