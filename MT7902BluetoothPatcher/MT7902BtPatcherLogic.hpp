// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Pure, host-testable HCI state machine & USB ID filter for MT7902BluetoothPatcher.

#ifndef MT7902_BT_PATCHER_LOGIC_HPP
#define MT7902_BT_PATCHER_LOGIC_HPP

#include <stddef.h>
#include <stdint.h>

namespace mt7902 {
namespace bt {

static constexpr uint16_t kHciOpReset = 0x0C03;
static constexpr uint16_t kHciOpLeSetRandomAddress = 0x2005;
static constexpr uint16_t kHciOpLeSetScanParam = 0x200B;
static constexpr uint16_t kHciOpLeSetScanEnable = 0x200C;
static constexpr uint16_t kHciOpLeSetExtScanParam = 0x2041;
static constexpr uint16_t kHciOpLeSetExtScanEnable = 0x2042;

// 9-byte HCI_LE_Set_Random_Address command (opcode 0x2005 LE, plen 6, valid Static Random Address
// whose MSB byte 0xDB has bits [7:6] == 11 per Bluetooth Core Spec Vol 6 Part B §1.3.2.1).
static constexpr uint8_t kStaticRandomAddressHciCmd[9] = {
    0x05, 0x20, 0x06,
    0x94, 0x50, 0x64, 0xD0, 0x78, 0xDB
};

struct __attribute__((packed)) HciCmdHeader {
    uint16_t opcode;
    uint8_t paramLen;
};

static inline bool isMT7902BtUsbId(uint16_t vid, uint16_t pid) {
    if (vid == 0x13D3) {
        return pid == 0x3579 || pid == 0x3575 || pid == 0x3584 || pid == 0x3596;
    }
    if (vid == 0x0489) {
        return pid == 0xE0F2 || pid == 0xE0F5;
    }
    if (vid == 0x0E8D) {
        return pid == 0x7902;
    }
    return false;
}

static inline bool isClassOutControlRequest(uint8_t bmRequestType) {
    // Direction = OUT (bit 7 == 0), Type = Class (bits [6:5] == 01 -> 0x20)
    return (bmRequestType & 0xE0U) == 0x20U;
}

static inline bool isBluetoothVmBypassProcess(const char *procName) {
    if (!procName)
        return false;
    // Manual compare so header has zero libc/libkern header dependencies
    auto eq = [](const char *a, const char *b) -> bool {
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    };
    return eq(procName, "bluetoothd") || eq(procName, "BlueTool");
}

class HciRandomAddrPatcherState {
public:
    void reset() { mRandomAddrSet = false; }
    bool isRandomAddrSet() const { return mRandomAddrSet; }

    // Inspect an outgoing HCI command opcode.
    // Returns true if the caller must inject HCI_LE_Set_Random_Address BEFORE forwarding this command.
    bool onOutgoingHciOpcode(uint16_t opcode) {
        if (opcode == kHciOpReset) {
            mRandomAddrSet = false;
            return false;
        }
        if (opcode == kHciOpLeSetRandomAddress) {
            mRandomAddrSet = true;
            return false;
        }
        if ((opcode == kHciOpLeSetScanParam || opcode == kHciOpLeSetExtScanParam) && !mRandomAddrSet) {
            mRandomAddrSet = true;
            return true;
        }
        return false;
    }

private:
    bool mRandomAddrSet = false;
};

} // namespace bt
} // namespace mt7902

#endif // MT7902_BT_PATCHER_LOGIC_HPP
