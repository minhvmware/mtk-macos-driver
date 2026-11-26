// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Self-contained IEEE 802.11i WPA2-PSK / SHA256-PSK 4-Way & Group EAPOL Handshake Engine
// Required for macOS 15 Sequoia & macOS 26 Tahoe WCL (which supplies 32-byte PMK in-kernel)

#ifndef MT7902_WPA2_HPP
#define MT7902_WPA2_HPP

#include "../../include/MT7902Common.hpp"
#include "../HAL/MT7902McuTypes.hpp"

namespace mt7902 {

struct Wpa2Ptk {
    uint8_t kck[16] = {};
    uint8_t kek[16] = {};
    uint8_t tk[16] = {};
};

struct Wpa2GtkResult {
    bool found = false;
    uint8_t keyIdx = 1;
    uint8_t keyLen = 0;
    uint8_t key[32] = {};
    uint8_t rsc[6] = {};
};

enum Wpa2StepResult : int {
    kWpa2Ignored = 0,
    kWpa2SentMsg2 = 1,
    kWpa2Completed4Way = 2,
    kWpa2CompletedGroupRekey = 3,
    kWpa2Error = -1,
};

class MT7902Wpa2 {
public:
    // Cryptographic primitives (exposed for deterministic RFC/IEEE unit tests)
    static void sha1(const uint8_t *data, size_t len, uint8_t digest[20]);
    static void hmacSha1(const uint8_t *key, size_t keyLen,
                         const uint8_t *data, size_t dataLen, uint8_t mac[20]);
    static void sha256(const uint8_t *data, size_t len, uint8_t digest[32]);
    static void hmacSha256(const uint8_t *key, size_t keyLen,
                           const uint8_t *data, size_t dataLen, uint8_t mac[32]);

    static void aes128EncryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
    static void aes128DecryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
    static void aes128Cmac(const uint8_t key[16], const uint8_t *data, size_t len, uint8_t mac[16]);

    static bool aesKeyWrap(const uint8_t kek[16], const uint8_t *plain, size_t plainLen,
                           uint8_t *cipherOut, size_t &cipherLenOut);
    static bool aesKeyUnwrap(const uint8_t kek[16], const uint8_t *cipher, size_t cipherLen,
                             uint8_t *plainOut, size_t &plainLenOut);

    // Derive 48-byte PTK (KCK || KEK || TK) for Key Descriptor Version 2 (SHA1 PRF-384) or 3 (SHA256 KDF-384)
    static void derivePtk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6],
                          const uint8_t anonce[32], const uint8_t snonce[32],
                          uint8_t keyDescVer, Wpa2Ptk &outPtk);

    // Compute 16-byte EAPOL-Key MIC over `eapolFrame[0..eapolLen)` (MIC field at offset 81 must be 0)
    static bool computeKeyMic(const uint8_t kck[16], uint8_t keyDescVer,
                              const uint8_t *eapolFrame, size_t eapolLen, uint8_t outMic[16]);

    // Build a clean STA RSN IE matching the AP's group cipher & PSK AKM from AP raw beacon IEs
    static size_t buildStaRsnIeFromAp(const uint8_t *apIes, size_t apIeLen,
                                      uint8_t outRsnIe[64], uint8_t &outGroupCipher);
};

struct Wpa2Session {
    bool active = false;
    uint8_t pmk[32] = {};
    uint8_t staMac[6] = {};
    uint8_t bssid[6] = {};
    uint8_t staRsnIe[64] = {};
    uint16_t staRsnIeLen = 0;
    uint8_t groupCipher = MCU_CIPHER_AES_CCMP;

    uint8_t snonce[32] = {};
    uint8_t anonce[32] = {};
    Wpa2Ptk ptk = {};
    bool ptkValid = false;
    bool haveReplay = false;
    uint64_t lastReplay = 0;
    uint32_t nonceCounter = 1;

    void reset();
    void start(const uint8_t inPmk[32], const uint8_t inStaMac[6], const uint8_t inBssid[6],
               const uint8_t *rsnIe, uint16_t rsnLen, uint8_t grpCipher, uint64_t seedMs);

    // Process an inbound 802.3 Ethernet frame with EtherType 0x888E.
    // On kWpa2SentMsg2 / kWpa2Completed4Way / kWpa2CompletedGroupRekey, `outTxEth` holds the reply 802.3 EAPOL frame.
    Wpa2StepResult processRxEapol(const uint8_t *rxEth, size_t rxEthLen,
                                  uint8_t *outTxEth, size_t outTxCap, size_t &outTxLen,
                                  Wpa2GtkResult &outGtk);
};

} // namespace mt7902

#endif // MT7902_WPA2_HPP
