// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#if defined(KERNEL) && !defined(IO80211FAMILY_V2)

#include "AirportMT7902Interface.hpp"

#define super IO80211Interface
OSDefineMetaClassAndStructors(AirportMT7902Interface, IO80211Interface);

bool AirportMT7902Interface::init(IO80211Controller *controller) {
    return super::init(controller);
}

UInt32 AirportMT7902Interface::inputPacket(mbuf_t packet, UInt32 length,
                                          IOOptionBits options, void *param) {
    if (!packet)
        return 0;
    return super::inputPacket(packet, length, options, param);
}

#endif // KERNEL && !IO80211FAMILY_V2
