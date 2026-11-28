// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include "MT7902Wpa2.hpp"

namespace mt7902 {

namespace {

static inline uint32_t rotl32(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32U - n));
}

static inline uint32_t rotr32(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32U - n));
}

static inline uint32_t rdBe32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static inline void wrBe32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

static inline uint64_t rdBe64(const uint8_t *p) {
    return (static_cast<uint64_t>(rdBe32(p)) << 32) | rdBe32(p + 4);
}

static inline void wrBe64(uint8_t *p, uint64_t v) {
    wrBe32(p, static_cast<uint32_t>(v >> 32));
    wrBe32(p + 4, static_cast<uint32_t>(v));
}

// --- Minimal constant-table AES-128 ---
static const uint8_t kAesSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t kAesInvSbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static inline uint8_t gfMul2(uint8_t x) {
    return static_cast<uint8_t>((x << 1) ^ ((x & 0x80U) ? 0x1bU : 0x00U));
}

static inline uint8_t gfMul(uint8_t x, uint8_t y) {
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i) {
        if (y & 1U)
            r ^= x;
        x = gfMul2(x);
        y >>= 1;
    }
    return r;
}

static void aes128KeySchedule(const uint8_t key[16], uint8_t rk[176]) {
    memcpy(rk, key, 16);
    uint8_t rcon = 0x01;
    for (int i = 4; i < 44; ++i) {
        uint8_t tmp[4];
        memcpy(tmp, rk + (i - 1) * 4, 4);
        if ((i & 3) == 0) {
            uint8_t t = tmp[0];
            tmp[0] = kAesSbox[tmp[1]] ^ rcon;
            tmp[1] = kAesSbox[tmp[2]];
            tmp[2] = kAesSbox[tmp[3]];
            tmp[3] = kAesSbox[t];
            rcon = gfMul2(rcon);
        }
        for (int j = 0; j < 4; ++j)
            rk[i * 4 + j] = rk[(i - 4) * 4 + j] ^ tmp[j];
    }
}

static const uint32_t kSha256K[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};

} // namespace

void MT7902Wpa2::sha1(const uint8_t *data, size_t len, uint8_t digest[20]) {
    uint32_t h0 = 0x67452301U;
    uint32_t h1 = 0xefcdab89U;
    uint32_t h2 = 0x98badcfeU;
    uint32_t h3 = 0x10325476U;
    uint32_t h4 = 0xc3d2e1f0U;

    auto processBlock = [&](const uint8_t blk[64]) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = rdBe32(blk + i * 4);
        for (int i = 16; i < 80; ++i)
            w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5a827999U; }
            else if (i < 40) { f = b ^ c ^ d;            k = 0x6ed9eba1U; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcU; }
            else             { f = b ^ c ^ d;            k = 0xca62c1d6U; }
            uint32_t tmp = rotl32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl32(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    };

    size_t off = 0;
    while (off + 64 <= len) {
        processBlock(data + off);
        off += 64;
    }

    uint8_t tail[128] = {};
    size_t rem = len - off;
    if (rem > 0)
        memcpy(tail, data + off, rem);
    tail[rem] = 0x80;
    size_t padLen = (rem < 56) ? 64 : 128;
    wrBe64(tail + padLen - 8, static_cast<uint64_t>(len) * 8ULL);
    processBlock(tail);
    if (padLen == 128)
        processBlock(tail + 64);

    wrBe32(digest + 0, h0);
    wrBe32(digest + 4, h1);
    wrBe32(digest + 8, h2);
    wrBe32(digest + 12, h3);
    wrBe32(digest + 16, h4);
}

void MT7902Wpa2::hmacSha1(const uint8_t *key, size_t keyLen,
                          const uint8_t *data, size_t dataLen, uint8_t mac[20]) {
    uint8_t k0[64] = {};
    if (keyLen > 64)
        sha1(key, keyLen, k0);
    else if (keyLen > 0)
        memcpy(k0, key, keyLen);

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<uint8_t>(k0[i] ^ 0x36U);
        opad[i] = static_cast<uint8_t>(k0[i] ^ 0x5cU);
    }

    // Max EAPOL / PRF message here is <= 1600 bytes
    uint8_t innerBuf[64 + 1600];
    size_t copyLen = dataLen > 1600 ? 1600 : dataLen;
    memcpy(innerBuf, ipad, 64);
    if (copyLen > 0)
        memcpy(innerBuf + 64, data, copyLen);
    uint8_t innerHash[20];
    sha1(innerBuf, 64 + copyLen, innerHash);

    uint8_t outerBuf[64 + 20];
    memcpy(outerBuf, opad, 64);
    memcpy(outerBuf + 64, innerHash, 20);
    sha1(outerBuf, sizeof(outerBuf), mac);
}

void MT7902Wpa2::sha256(const uint8_t *data, size_t len, uint8_t digest[32]) {
    uint32_t h[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };

    auto processBlock = [&](const uint8_t blk[64]) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = rdBe32(blk + i * 4);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + kSha256K[i] + w[i];
            uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    };

    size_t off = 0;
    while (off + 64 <= len) {
        processBlock(data + off);
        off += 64;
    }

    uint8_t tail[128] = {};
    size_t rem = len - off;
    if (rem > 0)
        memcpy(tail, data + off, rem);
    tail[rem] = 0x80;
    size_t padLen = (rem < 56) ? 64 : 128;
    wrBe64(tail + padLen - 8, static_cast<uint64_t>(len) * 8ULL);
    processBlock(tail);
    if (padLen == 128)
        processBlock(tail + 64);

    for (int i = 0; i < 8; ++i)
        wrBe32(digest + i * 4, h[i]);
}

void MT7902Wpa2::hmacSha256(const uint8_t *key, size_t keyLen,
                            const uint8_t *data, size_t dataLen, uint8_t mac[32]) {
    uint8_t k0[64] = {};
    if (keyLen > 64)
        sha256(key, keyLen, k0);
    else if (keyLen > 0)
        memcpy(k0, key, keyLen);

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<uint8_t>(k0[i] ^ 0x36U);
        opad[i] = static_cast<uint8_t>(k0[i] ^ 0x5cU);
    }

    uint8_t innerBuf[64 + 1600];
    size_t copyLen = dataLen > 1600 ? 1600 : dataLen;
    memcpy(innerBuf, ipad, 64);
    if (copyLen > 0)
        memcpy(innerBuf + 64, data, copyLen);
    uint8_t innerHash[32];
    sha256(innerBuf, 64 + copyLen, innerHash);

    uint8_t outerBuf[64 + 32];
    memcpy(outerBuf, opad, 64);
    memcpy(outerBuf + 64, innerHash, 32);
    sha256(outerBuf, sizeof(outerBuf), mac);
}

void MT7902Wpa2::aes128EncryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    uint8_t rk[176];
    aes128KeySchedule(key, rk);

    uint8_t s[16];
    for (int i = 0; i < 16; ++i)
        s[i] = in[i] ^ rk[i];

    for (int r = 1; r <= 10; ++r) {
        uint8_t t[16];
        for (int i = 0; i < 16; ++i)
            t[i] = kAesSbox[s[i]];

        // ShiftRows (column-major)
        s[0]  = t[0];  s[4]  = t[4];  s[8]  = t[8];  s[12] = t[12];
        s[1]  = t[5];  s[5]  = t[9];  s[9]  = t[13]; s[13] = t[1];
        s[2]  = t[10]; s[6]  = t[14]; s[10] = t[2];  s[14] = t[6];
        s[3]  = t[15]; s[7]  = t[3];  s[11] = t[7];  s[15] = t[11];

        if (r < 10) {
            for (int c = 0; c < 4; ++c) {
                uint8_t *col = s + c * 4;
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                uint8_t xr = a0 ^ a1 ^ a2 ^ a3;
                col[0] ^= xr ^ gfMul2(a0 ^ a1);
                col[1] ^= xr ^ gfMul2(a1 ^ a2);
                col[2] ^= xr ^ gfMul2(a2 ^ a3);
                col[3] ^= xr ^ gfMul2(a3 ^ a0);
            }
        }

        for (int i = 0; i < 16; ++i)
            s[i] ^= rk[r * 16 + i];
    }
    memcpy(out, s, 16);
}

void MT7902Wpa2::aes128DecryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    uint8_t rk[176];
    aes128KeySchedule(key, rk);

    uint8_t s[16];
    for (int i = 0; i < 16; ++i)
        s[i] = in[i] ^ rk[160 + i];

    for (int r = 9; r >= 0; --r) {
        uint8_t t[16];
        // InvShiftRows
        t[0]  = s[0];  t[4]  = s[4];  t[8]  = s[8];  t[12] = s[12];
        t[1]  = s[13]; t[5]  = s[1];  t[9]  = s[5];  t[13] = s[9];
        t[2]  = s[10]; t[6]  = s[14]; t[10] = s[2];  t[14] = s[6];
        t[3]  = s[7];  t[7]  = s[11]; t[11] = s[15]; t[15] = s[3];

        for (int i = 0; i < 16; ++i)
            s[i] = kAesInvSbox[t[i]] ^ rk[r * 16 + i];

        if (r > 0) {
            for (int c = 0; c < 4; ++c) {
                uint8_t *col = s + c * 4;
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = gfMul(a0, 0x0e) ^ gfMul(a1, 0x0b) ^ gfMul(a2, 0x0d) ^ gfMul(a3, 0x09);
                col[1] = gfMul(a0, 0x09) ^ gfMul(a1, 0x0e) ^ gfMul(a2, 0x0b) ^ gfMul(a3, 0x0d);
                col[2] = gfMul(a0, 0x0d) ^ gfMul(a1, 0x09) ^ gfMul(a2, 0x0e) ^ gfMul(a3, 0x0b);
                col[3] = gfMul(a0, 0x0b) ^ gfMul(a1, 0x0d) ^ gfMul(a2, 0x09) ^ gfMul(a3, 0x0e);
            }
        }
    }
    memcpy(out, s, 16);
}

static void dblBlock128(const uint8_t in[16], uint8_t out[16]) {
    uint8_t carry = 0;
    for (int i = 15; i >= 0; --i) {
        uint8_t next = (in[i] & 0x80U) ? 1U : 0U;
        out[i] = static_cast<uint8_t>((in[i] << 1) | carry);
        carry = next;
    }
    if (carry)
        out[15] ^= 0x87U;
}

void MT7902Wpa2::aes128Cmac(const uint8_t key[16], const uint8_t *data, size_t len, uint8_t mac[16]) {
    uint8_t zero[16] = {}, L[16] = {}, K1[16] = {}, K2[16] = {};
    aes128EncryptBlock(key, zero, L);
    dblBlock128(L, K1);
    dblBlock128(K1, K2);

    size_t n = (len == 0) ? 1 : ((len + 15) / 16);
    bool complete = (len > 0) && ((len % 16) == 0);

    uint8_t x[16] = {};
    for (size_t i = 0; i + 1 < n; ++i) {
        for (int b = 0; b < 16; ++b)
            x[b] ^= data[i * 16 + b];
        aes128EncryptBlock(key, x, x);
    }

    uint8_t last[16] = {};
    size_t rem = len - (n - 1) * 16;
    if (complete) {
        for (int b = 0; b < 16; ++b)
            last[b] = data[(n - 1) * 16 + b] ^ K1[b];
    } else {
        if (rem > 0)
            memcpy(last, data + (n - 1) * 16, rem);
        last[rem] = 0x80;
        for (int b = 0; b < 16; ++b)
            last[b] ^= K2[b];
    }
    for (int b = 0; b < 16; ++b)
        x[b] ^= last[b];
    aes128EncryptBlock(key, x, mac);
}

bool MT7902Wpa2::aesKeyWrap(const uint8_t kek[16], const uint8_t *plain, size_t plainLen,
                            uint8_t *cipherOut, size_t &cipherLenOut) {
    cipherLenOut = 0;
    if (!plain || !cipherOut || plainLen < 16 || (plainLen % 8) != 0 || plainLen > 512)
        return false;
    size_t n = plainLen / 8;
    uint64_t A = 0xa6a6a6a6a6a6a6a6ULL;
    memcpy(cipherOut + 8, plain, plainLen);

    for (size_t j = 0; j <= 5; ++j) {
        for (size_t i = 1; i <= n; ++i) {
            uint8_t blk[16], enc[16];
            wrBe64(blk, A);
            memcpy(blk + 8, cipherOut + i * 8, 8);
            aes128EncryptBlock(kek, blk, enc);
            uint64_t t = static_cast<uint64_t>(n * j + i);
            A = rdBe64(enc) ^ t;
            memcpy(cipherOut + i * 8, enc + 8, 8);
        }
    }
    wrBe64(cipherOut, A);
    cipherLenOut = plainLen + 8;
    return true;
}

bool MT7902Wpa2::aesKeyUnwrap(const uint8_t kek[16], const uint8_t *cipher, size_t cipherLen,
                              uint8_t *plainOut, size_t &plainLenOut) {
    plainLenOut = 0;
    if (!cipher || !plainOut || cipherLen < 24 || (cipherLen % 8) != 0 || cipherLen > 520)
        return false;

    size_t n = (cipherLen / 8) - 1;
    uint64_t A = rdBe64(cipher);
    memcpy(plainOut, cipher + 8, n * 8);

    for (int j = 5; j >= 0; --j) {
        for (size_t i = n; i >= 1; --i) {
            uint64_t t = static_cast<uint64_t>(n * static_cast<size_t>(j) + i);
            uint8_t blk[16], dec[16];
            wrBe64(blk, A ^ t);
            memcpy(blk + 8, plainOut + (i - 1) * 8, 8);
            aes128DecryptBlock(kek, blk, dec);
            A = rdBe64(dec);
            memcpy(plainOut + (i - 1) * 8, dec + 8, 8);
        }
    }

    if (A != 0xa6a6a6a6a6a6a6a6ULL)
        return false;
    plainLenOut = n * 8;
    return true;
}

void MT7902Wpa2::derivePtk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6],
                           const uint8_t anonce[32], const uint8_t snonce[32],
                           uint8_t keyDescVer, Wpa2Ptk &outPtk) {
    outPtk = {};
    uint8_t b[76];
    if (memcmp(aa, spa, 6) < 0) {
        memcpy(b + 0, aa, 6);
        memcpy(b + 6, spa, 6);
    } else {
        memcpy(b + 0, spa, 6);
        memcpy(b + 6, aa, 6);
    }
    if (memcmp(anonce, snonce, 32) < 0) {
        memcpy(b + 12, anonce, 32);
        memcpy(b + 44, snonce, 32);
    } else {
        memcpy(b + 12, snonce, 32);
        memcpy(b + 44, anonce, 32);
    }

    static const char kLabel[] = "Pairwise key expansion"; // 22 chars
    uint8_t raw[64] = {};

    if (keyDescVer >= 3) {
        // IEEE 802.11 KDF-SHA256-384: HMAC-SHA256(PMK, LE16(i) || label || B || LE16(384))
        for (uint16_t iter = 1; iter <= 2; ++iter) {
            uint8_t msg[2 + 22 + 76 + 2];
            putLe16(msg + 0, iter);
            memcpy(msg + 2, kLabel, 22);
            memcpy(msg + 24, b, 76);
            putLe16(msg + 100, 384);
            hmacSha256(pmk, 32, msg, sizeof(msg), raw + (iter - 1) * 32);
        }
    } else {
        // IEEE 802.11i PRF-384: HMAC-SHA1(PMK, label || 0x00 || B || byte(i)) for i = 0..2
        for (uint8_t iter = 0; iter < 3; ++iter) {
            uint8_t msg[23 + 76 + 1];
            memcpy(msg, kLabel, 23); // includes trailing '\0' separator
            memcpy(msg + 23, b, 76);
            msg[99] = iter;
            hmacSha1(pmk, 32, msg, sizeof(msg), raw + iter * 20);
        }
    }

    memcpy(outPtk.kck, raw + 0, 16);
    memcpy(outPtk.kek, raw + 16, 16);
    memcpy(outPtk.tk, raw + 32, 16);
}

bool MT7902Wpa2::computeKeyMic(const uint8_t kck[16], uint8_t keyDescVer,
                               const uint8_t *eapolFrame, size_t eapolLen, uint8_t outMic[16]) {
    if (!eapolFrame || eapolLen < 99 || eapolLen > 1500)
        return false;
    uint8_t copy[1500];
    memcpy(copy, eapolFrame, eapolLen);
    memset(copy + 81, 0, 16);

    if (keyDescVer == 2) {
        uint8_t mac20[20];
        hmacSha1(kck, 16, copy, eapolLen, mac20);
        memcpy(outMic, mac20, 16);
        return true;
    } else if (keyDescVer == 3) {
        aes128Cmac(kck, copy, eapolLen, outMic);
        return true;
    }
    return false;
}

size_t MT7902Wpa2::buildStaRsnIeFromAp(const uint8_t *apIes, size_t apIeLen,
                                       uint8_t outRsnIe[64], uint8_t &outGroupCipher) {
    // Default group CCMP-128, pairwise CCMP-128, AKM PSK (00:0f:ac:02), RSN caps 0x0080 (MFPC) or 0x0000
    outGroupCipher = MCU_CIPHER_AES_CCMP;
    uint8_t grpSuite[4] = { 0x00, 0x0f, 0xac, 0x04 };
    uint8_t akmSuite[4] = { 0x00, 0x0f, 0xac, 0x02 };
    uint16_t rsnCaps = 0x0000;

    size_t pos = 0;
    while (apIes && pos + 2 <= apIeLen) {
        uint8_t id = apIes[pos];
        uint8_t elen = apIes[pos + 1];
        if (pos + 2 + elen > apIeLen)
            break;
        if (id == 48 && elen >= 8) {
            const uint8_t *b = apIes + pos + 2;
            if (getLe16(b) == 1) {
                memcpy(grpSuite, b + 2, 4);
                if (grpSuite[0] == 0x00 && grpSuite[1] == 0x0f && grpSuite[2] == 0xac && grpSuite[3] == 0x02)
                    outGroupCipher = MCU_CIPHER_TKIP;

                size_t p = 6;
                uint16_t pwCnt = getLe16(b + p);
                p += 2 + static_cast<size_t>(pwCnt) * 4;
                if (p + 2 <= elen) {
                    uint16_t akmCnt = getLe16(b + p);
                    p += 2;
                    bool hasPsk2 = false, hasPskSha256 = false;
                    for (uint16_t i = 0; i < akmCnt && p + 4 <= elen; ++i, p += 4) {
                        if (b[p] == 0x00 && b[p + 1] == 0x0f && b[p + 2] == 0xac) {
                            if (b[p + 3] == 2) hasPsk2 = true;
                            else if (b[p + 3] == 6) hasPskSha256 = true;
                        }
                    }
                    if (!hasPsk2 && hasPskSha256)
                        akmSuite[3] = 6;
                    if (p + 2 <= elen) {
                        uint16_t apCaps = getLe16(b + p);
                        // Only advertise MFPC when AP REQUIRES PMF (MFPR = BIT(6)); on WPA2/WPA3
                        // transition APs (MFPC=1, MFPR=0), WPA2-PSK runs without PMF so deauth/mgmt
                        // frames are accepted cleanly without IGTK/SA-Query overhead.
                        if (apCaps & 0x0040U)
                            rsnCaps = 0x0080U;
                    }
                }
            }
            break;
        }
        pos += 2 + elen;
    }

    outRsnIe[0] = 48;
    outRsnIe[1] = 20;
    putLe16(outRsnIe + 2, 1);
    memcpy(outRsnIe + 4, grpSuite, 4);
    putLe16(outRsnIe + 8, 1);
    outRsnIe[10] = 0x00; outRsnIe[11] = 0x0f; outRsnIe[12] = 0xac; outRsnIe[13] = 0x04;
    putLe16(outRsnIe + 14, 1);
    memcpy(outRsnIe + 16, akmSuite, 4);
    putLe16(outRsnIe + 20, rsnCaps);
    return 22;
}

void Wpa2Session::reset() {
    *this = Wpa2Session();
}

void Wpa2Session::start(const uint8_t inPmk[32], const uint8_t inStaMac[6], const uint8_t inBssid[6],
                        const uint8_t *rsnIe, uint16_t rsnLen, uint8_t grpCipher, uint64_t seedMs) {
    reset();
    active = true;
    memcpy(pmk, inPmk, 32);
    memcpy(staMac, inStaMac, 6);
    memcpy(bssid, inBssid, 6);
    groupCipher = grpCipher;
    if (rsnIe && rsnLen > 0 && rsnLen <= sizeof(staRsnIe)) {
        memcpy(staRsnIe, rsnIe, rsnLen);
        staRsnIeLen = rsnLen;
    }

    // Generate pseudo-random SNonce seeded with PMK + MACs + monotonic clock
    uint8_t seedBuf[32 + 12 + 8];
    memcpy(seedBuf, inPmk, 32);
    memcpy(seedBuf + 32, inStaMac, 6);
    memcpy(seedBuf + 38, inBssid, 6);
    wrBe64(seedBuf + 44, seedMs ^ 0x9e3779b97f4a7c15ULL);
    MT7902Wpa2::sha256(seedBuf, sizeof(seedBuf), snonce);
}

static bool extractGtkKde(const uint8_t *plain, size_t plainLen, Wpa2GtkResult &outGtk) {
    outGtk = {};
    size_t pos = 0;
    while (pos + 2 <= plainLen) {
        uint8_t id = plain[pos];
        uint8_t elen = plain[pos + 1];
        if (id == 0xdd && elen == 0)
            break; // IEEE 802.11i Key Data padding (0xdd 0x00 ...)
        if (pos + 2 + elen > plainLen)
            break;
        const uint8_t *body = plain + pos + 2;
        if (id == 0xdd && elen >= 6 &&
            body[0] == 0x00 && body[1] == 0x0f && body[2] == 0xac && body[3] == 0x01) {
            size_t gtkLen = elen - 6;
            if (gtkLen >= 16 && gtkLen <= 32) {
                outGtk.found = true;
                outGtk.keyIdx = body[4] & 0x03U;
                outGtk.keyLen = static_cast<uint8_t>(gtkLen);
                memcpy(outGtk.key, body + 6, gtkLen);
                // Swap TKIP RX/TX Michael MIC keys [16..23] <-> [24..31] for STA RX/TX convention if 32B TKIP
                if (gtkLen == 32) {
                    uint8_t tmp[8];
                    memcpy(tmp, outGtk.key + 16, 8);
                    memcpy(outGtk.key + 16, outGtk.key + 24, 8);
                    memcpy(outGtk.key + 24, tmp, 8);
                }
                return true;
            }
        }
        pos += 2 + elen;
    }
    return false;
}

Wpa2StepResult Wpa2Session::processRxEapol(const uint8_t *rxEth, size_t rxEthLen,
                                           uint8_t *outTxEth, size_t outTxCap, size_t &outTxLen,
                                           Wpa2GtkResult &outGtk) {
    outTxLen = 0;
    outGtk = {};
    if (!active || !rxEth || rxEthLen < kEthHdrLen + 99)
        return kWpa2Ignored;
    if (getBe16(rxEth + 12) != 0x888e)
        return kWpa2Ignored;

    const uint8_t *eapol = rxEth + kEthHdrLen;
    size_t availLen = rxEthLen - kEthHdrLen;
    if (eapol[1] != 3) // EAPOL-Key
        return kWpa2Ignored;

    uint16_t bodyLen = getBe16(eapol + 2);
    size_t eapolTotal = 4 + static_cast<size_t>(bodyLen);
    if (eapolTotal < 99 || eapolTotal > availLen)
        return kWpa2Ignored;

    uint8_t descType = eapol[4];
    if (descType != 2 && descType != 254)
        return kWpa2Ignored;

    uint16_t keyInfo = getBe16(eapol + 5);
    uint8_t keyVer = static_cast<uint8_t>(keyInfo & 0x0007U);
    bool isPairwise = (keyInfo & 0x0008U) != 0;
    bool hasAck     = (keyInfo & 0x0080U) != 0;
    bool hasMic     = (keyInfo & 0x0100U) != 0;
    bool isSecure   = (keyInfo & 0x0200U) != 0;
    bool isEncData  = (keyInfo & 0x1000U) != 0;

    if (!hasAck || (keyVer != 2 && keyVer != 3))
        return kWpa2Ignored;

    uint64_t replay = rdBe64(eapol + 9);
    if (haveReplay && replay <= lastReplay && hasMic)
        return kWpa2Ignored;

    uint16_t keyDataLen = getBe16(eapol + 97);
    if (99 + static_cast<size_t>(keyDataLen) > eapolTotal)
        return kWpa2Error;

    // Case 1: 4-Way Handshake Message 1/4 (Pairwise=1, Ack=1, MIC=0)
    if (isPairwise && !hasMic) {
        // Per IEEE 802.11-2020 §12.7.6.2, reuse the same SNonce & PTK on retransmitted Msg 1/4
        // carrying the identical ANonce so Msg 3/4 MIC never mismatches the AP's derived PTK.
        bool sameAnonce = ptkValid && (memcmp(anonce, eapol + 17, 32) == 0);
        if (!sameAnonce) {
            memcpy(anonce, eapol + 17, 32);
            if (ptkValid) {
                wrBe32(snonce + 28, rdBe32(snonce + 28) + (nonceCounter++));
            }
            MT7902Wpa2::derivePtk(pmk, bssid, staMac, anonce, snonce, keyVer, ptk);
            ptkValid = true;
        }

        size_t msg2BodyLen = 95 + staRsnIeLen;
        size_t msg2EapolLen = 4 + msg2BodyLen;
        if (kEthHdrLen + msg2EapolLen > outTxCap)
            return kWpa2Error;

        memset(outTxEth, 0, kEthHdrLen + msg2EapolLen);
        memcpy(outTxEth + 0, bssid, 6);
        memcpy(outTxEth + 6, staMac, 6);
        putBe16(outTxEth + 12, 0x888e);

        uint8_t *txEapol = outTxEth + kEthHdrLen;
        txEapol[0] = 1; // IEEE 802.1X-2001 version 1 (widest AP compatibility)
        txEapol[1] = 3; // EAPOL-Key
        putBe16(txEapol + 2, static_cast<uint16_t>(msg2BodyLen));
        txEapol[4] = descType;
        uint16_t msg2Info = static_cast<uint16_t>(keyVer | 0x0008U | 0x0100U); // Pairwise | Key MIC
        putBe16(txEapol + 5, msg2Info);
        putBe16(txEapol + 7, 0);
        memcpy(txEapol + 9, eapol + 9, 8); // Echo Replay Counter
        memcpy(txEapol + 17, snonce, 32);
        putBe16(txEapol + 97, staRsnIeLen);
        if (staRsnIeLen > 0)
            memcpy(txEapol + 99, staRsnIe, staRsnIeLen);

        uint8_t mic[16];
        if (!MT7902Wpa2::computeKeyMic(ptk.kck, keyVer, txEapol, msg2EapolLen, mic))
            return kWpa2Error;
        memcpy(txEapol + 81, mic, 16);

        outTxLen = kEthHdrLen + msg2EapolLen;
        return kWpa2SentMsg2;
    }

    // All subsequent messages require a valid PTK and valid MIC
    if (!ptkValid || !hasMic)
        return kWpa2Ignored;

    uint8_t expectedMic[16];
    if (!MT7902Wpa2::computeKeyMic(ptk.kck, keyVer, eapol, eapolTotal, expectedMic) ||
        memcmp(expectedMic, eapol + 81, 16) != 0) {
        MTK_ERR("WPA2 EAPOL-Key MIC verification failed");
        return kWpa2Error;
    }

    haveReplay = true;
    lastReplay = replay;

    // Unwrap encrypted Key Data if present (Msg 3/4 or Group Msg 1/2)
    if (isEncData && keyDataLen >= 24) {
        uint8_t plain[512];
        size_t plainLen = 0;
        if (!MT7902Wpa2::aesKeyUnwrap(ptk.kek, eapol + 99, keyDataLen, plain, plainLen)) {
            MTK_ERR("WPA2 AES Key Unwrap failed on EAPOL Key Data");
            return kWpa2Error;
        }
        if (extractGtkKde(plain, plainLen, outGtk))
            memcpy(outGtk.rsc, eapol + 65, 6);
    }

    // Case 2: 4-Way Handshake Message 3/4 (Pairwise=1, Ack=1, MIC=1)
    if (isPairwise) {
        size_t msg4BodyLen = 95;
        size_t msg4EapolLen = 4 + msg4BodyLen;
        if (kEthHdrLen + msg4EapolLen > outTxCap)
            return kWpa2Error;

        memset(outTxEth, 0, kEthHdrLen + msg4EapolLen);
        memcpy(outTxEth + 0, bssid, 6);
        memcpy(outTxEth + 6, staMac, 6);
        putBe16(outTxEth + 12, 0x888e);

        uint8_t *txEapol = outTxEth + kEthHdrLen;
        txEapol[0] = 1;
        txEapol[1] = 3;
        putBe16(txEapol + 2, static_cast<uint16_t>(msg4BodyLen));
        txEapol[4] = descType;
        uint16_t msg4Info = static_cast<uint16_t>(keyVer | 0x0008U | 0x0100U | 0x0200U); // Pairwise | MIC | Secure
        putBe16(txEapol + 5, msg4Info);
        memcpy(txEapol + 9, eapol + 9, 8);

        uint8_t mic[16];
        if (!MT7902Wpa2::computeKeyMic(ptk.kck, keyVer, txEapol, msg4EapolLen, mic))
            return kWpa2Error;
        memcpy(txEapol + 81, mic, 16);

        outTxLen = kEthHdrLen + msg4EapolLen;
        return kWpa2Completed4Way;
    }

    // Case 3: 2-Way Group Key Handshake Message 1/2 (Pairwise=0, Ack=1, MIC=1, Secure=1)
    if (!isPairwise && isSecure) {
        size_t grp2BodyLen = 95;
        size_t grp2EapolLen = 4 + grp2BodyLen;
        if (kEthHdrLen + grp2EapolLen > outTxCap)
            return kWpa2Error;

        memset(outTxEth, 0, kEthHdrLen + grp2EapolLen);
        memcpy(outTxEth + 0, bssid, 6);
        memcpy(outTxEth + 6, staMac, 6);
        putBe16(outTxEth + 12, 0x888e);

        uint8_t *txEapol = outTxEth + kEthHdrLen;
        txEapol[0] = 1;
        txEapol[1] = 3;
        putBe16(txEapol + 2, static_cast<uint16_t>(grp2BodyLen));
        txEapol[4] = descType;
        uint16_t grp2Info = static_cast<uint16_t>(keyVer | 0x0100U | 0x0200U); // Group | MIC | Secure
        putBe16(txEapol + 5, grp2Info);
        memcpy(txEapol + 9, eapol + 9, 8);

        uint8_t mic[16];
        if (!MT7902Wpa2::computeKeyMic(ptk.kck, keyVer, txEapol, grp2EapolLen, mic))
            return kWpa2Error;
        memcpy(txEapol + 81, mic, 16);

        outTxLen = kEthHdrLen + grp2EapolLen;
        return kWpa2CompletedGroupRekey;
    }

    return kWpa2Ignored;
}

} // namespace mt7902
