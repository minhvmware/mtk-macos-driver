// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Native IO80211Interface subclass for macOS 12 Monterey / 13 Ventura

#ifndef AIRPORT_MT7902_INTERFACE_HPP
#define AIRPORT_MT7902_INTERFACE_HPP

#if defined(KERNEL) && !defined(IO80211FAMILY_V2)

#include <Airport/Apple80211.h>
#include <IOKit/IOLib.h>
#include <sys/kernel_types.h>

class AirportMT7902Interface : public IO80211Interface {
    OSDeclareDefaultStructors(AirportMT7902Interface)

public:
    bool init(IO80211Controller *controller);
    UInt32 inputPacket(mbuf_t packet, UInt32 length = 0,
                       IOOptionBits options = 0, void *param = 0) override;
};

#endif // KERNEL && !IO80211FAMILY_V2
#endif // AIRPORT_MT7902_INTERFACE_HPP
