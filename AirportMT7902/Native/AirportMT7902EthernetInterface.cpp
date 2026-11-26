// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#if defined(KERNEL) && defined(IO80211FAMILY_V2)

#include "AirportMT7902EthernetInterface.hpp"

#define super IOEthernetInterface
OSDefineMetaClassAndStructors(AirportMT7902EthernetInterface, IOEthernetInterface);

bool AirportMT7902EthernetInterface::initWithSkywalkInterfaceAndProvider(
    IONetworkController *controller, IO80211SkywalkInterface *interface) {
    if (!controller || !interface)
        return false;
    mSkywalkIf = interface;
    return super::init(controller);
}

IOReturn AirportMT7902EthernetInterface::attachToDataLinkLayer(IOOptionBits options, void *parameter) {
    IOReturn ret = super::attachToDataLinkLayer(options, parameter);
    if (ret == kIOReturnSuccess)
        mAttached = true;
    return ret;
}

void AirportMT7902EthernetInterface::detachFromDataLinkLayer(IOOptionBits options, void *parameter) {
    mAttached = false;
    super::detachFromDataLinkLayer(options, parameter);
}

UInt32 AirportMT7902EthernetInterface::inputPacket(mbuf_t packet, UInt32 length,
                                                   IOOptionBits options, void *param) {
    if (!packet || !mAttached)
        return 0;
    return super::inputPacket(packet, length, options, param);
}

IOService *AirportMT7902EthernetInterface::getProvider() const {
    return mSkywalkIf ? static_cast<IOService *>(mSkywalkIf) : super::getProvider();
}

bool AirportMT7902EthernetInterface::setLinkState(IO80211LinkState state) {
    (void)state;
    return mAttached;
}

#endif // KERNEL && IO80211FAMILY_V2
