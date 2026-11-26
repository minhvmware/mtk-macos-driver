// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// BSD Ethernet data-plane interface bridge for macOS 14 Sonoma / 15 Sequoia / 26 Tahoe

#ifndef AIRPORT_MT7902_ETHERNET_INTERFACE_HPP
#define AIRPORT_MT7902_ETHERNET_INTERFACE_HPP

#if defined(KERNEL) && defined(IO80211FAMILY_V2)

extern "C" {
#include <net/bpf.h>
}
#include <Airport/Apple80211.h>
#include <IOKit/IOLib.h>
#include <IOKit/network/IOEthernetInterface.h>

class AirportMT7902EthernetInterface : public IOEthernetInterface {
    OSDeclareDefaultStructors(AirportMT7902EthernetInterface)

public:
    bool initWithSkywalkInterfaceAndProvider(IONetworkController *controller,
                                             IO80211SkywalkInterface *interface);
    IOReturn attachToDataLinkLayer(IOOptionBits options, void *parameter) override;
    void detachFromDataLinkLayer(IOOptionBits options, void *parameter) override;
    UInt32 inputPacket(mbuf_t packet, UInt32 length = 0,
                       IOOptionBits options = 0, void *param = 0) override;
    IOService *getProvider() const override;

    bool setLinkState(IO80211LinkState state);

private:
    IO80211SkywalkInterface *mSkywalkIf = nullptr;
    bool mAttached = false;
};

#endif // KERNEL && IO80211FAMILY_V2
#endif // AIRPORT_MT7902_ETHERNET_INTERFACE_HPP
