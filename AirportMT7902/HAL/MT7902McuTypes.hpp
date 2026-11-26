// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Connac2 MCU headers, command IDs, firmware patch/RAM structures, and TLVs

#ifndef MT7902_MCU_TYPES_HPP
#define MT7902_MCU_TYPES_HPP

#include "../../include/MT7902Common.hpp"

namespace mt7902 {

// Firmware file names (matching Linux mt7921e + embedded pack_firmware.py names)
static constexpr const char *kMT7902PatchFwName = "WIFI_MT7902_patch_mcu_1_1_hdr.bin";
static constexpr const char *kMT7902RamFwName = "WIFI_RAM_CODE_MT7902_1.bin";

// MCU command encoding flags (matching mt76_connac_mcu.h)
static constexpr uint32_t MCU_CMD_FIELD_ID = genmask32(7, 0);
static constexpr uint32_t MCU_CMD_FIELD_EXT_ID = genmask32(15, 8);
static constexpr uint32_t MCU_CMD_FIELD_QUERY = MTK_BIT(16);
static constexpr uint32_t MCU_CMD_FIELD_UNI = MTK_BIT(17);
static constexpr uint32_t MCU_CMD_FIELD_CE = MTK_BIT(18);
static constexpr uint32_t MCU_CMD_FIELD_WA = MTK_BIT(19);

static constexpr uint32_t makeMcuCmd(uint8_t id) { return fieldPrep(MCU_CMD_FIELD_ID, id); }
static constexpr uint32_t makeMcuExtCmd(uint8_t ext) {
    return makeMcuCmd(0xed) | fieldPrep(MCU_CMD_FIELD_EXT_ID, ext);
}
static constexpr uint32_t makeMcuExtQuery(uint8_t ext) {
    return makeMcuExtCmd(ext) | MCU_CMD_FIELD_QUERY;
}
static constexpr uint32_t makeMcuUniCmd(uint8_t id) {
    return MCU_CMD_FIELD_UNI | fieldPrep(MCU_CMD_FIELD_ID, id);
}
static constexpr uint32_t makeMcuCeCmd(uint8_t id) {
    return MCU_CMD_FIELD_CE | fieldPrep(MCU_CMD_FIELD_ID, id);
}
static constexpr uint32_t makeMcuCeQuery(uint8_t id) {
    return makeMcuCeCmd(id) | MCU_CMD_FIELD_QUERY;
}

// Base MCU commands
static constexpr uint32_t MCU_CMD_TARGET_ADDRESS_LEN_REQ = makeMcuCmd(0x01);
static constexpr uint32_t MCU_CMD_FW_START_REQ = makeMcuCmd(0x02);
static constexpr uint32_t MCU_CMD_NIC_POWER_CTRL = makeMcuCmd(0x04);
static constexpr uint32_t MCU_CMD_PATCH_START_REQ = makeMcuCmd(0x05);
static constexpr uint32_t MCU_CMD_PATCH_FINISH_REQ = makeMcuCmd(0x07);
static constexpr uint32_t MCU_CMD_PATCH_SEM_CONTROL = makeMcuCmd(0x10);
static constexpr uint32_t MCU_CMD_FW_SCATTER = makeMcuCmd(0xee);

// Extended MCU commands
static constexpr uint32_t MCU_EXT_QUERY_EFUSE_ACCESS = makeMcuExtQuery(0x01);
static constexpr uint32_t MCU_EXT_CMD_PM_STATE_CTRL = makeMcuExtCmd(0x07);
static constexpr uint32_t MCU_EXT_CMD_CHANNEL_SWITCH = makeMcuExtCmd(0x08);
static constexpr uint32_t MCU_EXT_CMD_EFUSE_BUFFER_MODE = makeMcuExtCmd(0x21);
static constexpr uint32_t MCU_EXT_CMD_THERMAL_CTRL = makeMcuExtCmd(0x2c);
static constexpr uint32_t MCU_EXT_CMD_PROTECT_CTRL = makeMcuExtCmd(0x3e);
static constexpr uint32_t MCU_EXT_CMD_MAC_INIT_CTRL = makeMcuExtCmd(0x46);
static constexpr uint32_t MCU_EXT_CMD_SET_RX_PATH = makeMcuExtCmd(0x4e);

// Unified MCU commands (Connac2)
static constexpr uint32_t MCU_UNI_CMD_DEV_INFO_UPDATE = makeMcuUniCmd(0x01);
static constexpr uint32_t MCU_UNI_CMD_BSS_INFO_UPDATE = makeMcuUniCmd(0x02);
static constexpr uint32_t MCU_UNI_CMD_STA_REC_UPDATE = makeMcuUniCmd(0x03);
static constexpr uint32_t MCU_UNI_CMD_EDCA_UPDATE = makeMcuUniCmd(0x04);
static constexpr uint32_t MCU_UNI_CMD_SUSPEND = makeMcuUniCmd(0x05);
static constexpr uint32_t MCU_UNI_CMD_OFFLOAD = makeMcuUniCmd(0x06);
static constexpr uint32_t MCU_UNI_CMD_HIF_CTRL = makeMcuUniCmd(0x07);
static constexpr uint32_t MCU_UNI_CMD_ROC = makeMcuUniCmd(0x27);

// CE (Client Engine / Offload) MCU commands
static constexpr uint32_t MCU_CE_CMD_START_HW_SCAN = makeMcuCeCmd(0x03);
static constexpr uint32_t MCU_CE_CMD_SET_PS_PROFILE = makeMcuCeCmd(0x05);
static constexpr uint32_t MCU_CE_CMD_SET_RX_FILTER = makeMcuCeCmd(0x0a);
static constexpr uint32_t MCU_CE_CMD_SET_CHAN_DOMAIN = makeMcuCeCmd(0x0f);
static constexpr uint32_t MCU_CE_CMD_SET_BSS_CONNECTED = makeMcuCeCmd(0x16);
static constexpr uint32_t MCU_CE_CMD_SET_BSS_ABORT = makeMcuCeCmd(0x17);
static constexpr uint32_t MCU_CE_CMD_CANCEL_HW_SCAN = makeMcuCeCmd(0x1b);
static constexpr uint32_t MCU_CE_CMD_SET_EDCA_PARMS = makeMcuCeCmd(0x1d);
static constexpr uint32_t MCU_CE_CMD_SET_CLC = makeMcuCeCmd(0x5c);
static constexpr uint32_t MCU_CE_CMD_GET_NIC_CAPAB = makeMcuCeCmd(0x8a);
static constexpr uint32_t MCU_CE_CMD_REG_WRITE = makeMcuCeCmd(0xc0);
static constexpr uint32_t MCU_CE_QUERY_REG_READ = makeMcuCeQuery(0xc0);
static constexpr uint32_t MCU_CE_CMD_FWLOG_2_HOST = makeMcuCeCmd(0xc5);

// MCU Event IDs
static constexpr uint8_t MCU_EVENT_GENERIC = 0x01;
static constexpr uint8_t MCU_EVENT_MT_PATCH_SEM = 0x04;
static constexpr uint8_t MCU_EVENT_LP_INFO = 0x07;
static constexpr uint8_t MCU_EVENT_SCAN_DONE = 0x0d;
static constexpr uint8_t MCU_EVENT_TX_DONE = 0x0f;
static constexpr uint8_t MCU_EVENT_BSS_BEACON_LOSS = 0x13;
static constexpr uint8_t MCU_EVENT_SCHED_SCAN_DONE = 0x23;
static constexpr uint8_t MCU_EVENT_DBG_MSG = 0x27;
static constexpr uint8_t MCU_EVENT_RSSI_NOTIFY = 0x96;
static constexpr uint8_t MCU_EVENT_COREDUMP = 0xf0;
static constexpr uint8_t MCU_UNI_EVENT_ROC = 0x27;

static constexpr uint8_t MCU_UNI_CMD_UNSOLICITED_EVENT = MTK_BIT(2);
static constexpr uint8_t MCU_PKT_ID = 0xa0;
static constexpr uint8_t MCU_CMD_UNI_EXT_ACK = MTK_BIT(0) | MTK_BIT(1) | MTK_BIT(2);
static constexpr uint8_t MCU_S2D_H2N = 0x00;
static constexpr uint8_t MCU_S2D_H2C = 0x02;
static constexpr uint8_t MCU_Q_QUERY = 0;
static constexpr uint8_t MCU_Q_SET = 1;
static constexpr uint8_t MCU_Q_NA = 3;

static constexpr uint16_t mcuPqId(uint8_t port, uint8_t q) {
    return static_cast<uint16_t>((static_cast<uint16_t>(port) << 15) | (static_cast<uint16_t>(q) << 10));
}

// Firmware download flags
static constexpr uint8_t FW_FEATURE_SET_ENCRYPT = MTK_BIT(0);
static constexpr uint8_t FW_FEATURE_SET_KEY_IDX = static_cast<uint8_t>(genmask32(2, 1));
static constexpr uint8_t FW_FEATURE_ENCRY_MODE = MTK_BIT(4);
static constexpr uint8_t FW_FEATURE_OVERRIDE_ADDR = MTK_BIT(5);
static constexpr uint8_t FW_FEATURE_NON_DL = MTK_BIT(6);
static constexpr uint8_t FW_TYPE_CLC = 2;

static constexpr uint32_t DL_MODE_ENCRYPT = MTK_BIT(0);
static constexpr uint32_t DL_MODE_KEY_IDX = genmask32(2, 1);
static constexpr uint32_t DL_MODE_RESET_SEC_IV = MTK_BIT(3);
static constexpr uint32_t DL_MODE_WORKING_PDA_CR4 = MTK_BIT(4);
static constexpr uint32_t DL_CONFIG_ENCRY_MODE_SEL = MTK_BIT(6);
static constexpr uint32_t DL_MODE_NEED_RSP = MTK_BIT(31);

static constexpr uint32_t FW_START_OVERRIDE = MTK_BIT(0);
static constexpr uint32_t FW_START_WORKING_PDA_CR4 = MTK_BIT(2);

static constexpr uint32_t PATCH_SEC_NOT_SUPPORT = 0xffffffffU;
static constexpr uint32_t PATCH_SEC_TYPE_MASK = genmask32(15, 0);
static constexpr uint32_t PATCH_SEC_TYPE_INFO = 0x2;
static constexpr uint32_t PATCH_SEC_ENC_TYPE_MASK = genmask32(31, 24);
static constexpr uint32_t PATCH_SEC_ENC_TYPE_PLAIN = 0x00;
static constexpr uint32_t PATCH_SEC_ENC_TYPE_AES = 0x01;
static constexpr uint32_t PATCH_SEC_ENC_TYPE_SCRAMBLE = 0x02;
static constexpr uint32_t PATCH_SEC_ENC_AES_KEY_MASK = genmask32(7, 0);

static constexpr uint32_t MCU_PATCH_ADDRESS = 0x200000;
static constexpr uint32_t MCU_CONNAC2_PATCH_ADDR = 0x900000;

enum PatchSemOp : uint32_t {
    PATCH_SEM_RELEASE = 0,
    PATCH_SEM_GET = 1,
};

enum PatchSemStatus : uint8_t {
    PATCH_NOT_DL_SEM_FAIL = 0,
    PATCH_IS_DL = 1,
    PATCH_NOT_DL_SEM_SUCCESS = 2,
    PATCH_REL_SEM_SUCCESS = 3,
};

// Connac2 MCU descriptors (wire-exact)
struct __attribute__((packed, aligned(4))) Mt76Connac2McuTxd {
    uint32_t txd[8];
    uint16_t len;
    uint16_t pq_id;
    uint8_t cid;
    uint8_t pkt_type;
    uint8_t set_query;
    uint8_t seq;
    uint8_t uc_d2b0_rev;
    uint8_t ext_cid;
    uint8_t s2d_index;
    uint8_t ext_cid_ack;
    uint32_t rsv[5];
};
static_assert(sizeof(Mt76Connac2McuTxd) == 64, "Connac2 MCU TXD must be 64 bytes");

struct __attribute__((packed, aligned(4))) Mt76Connac2McuUniTxd {
    uint32_t txd[8];
    uint16_t len;
    uint16_t cid;
    uint8_t rsv;
    uint8_t pkt_type;
    uint8_t frag_n;
    uint8_t seq;
    uint16_t checksum;
    uint8_t s2d_index;
    uint8_t option;
    uint8_t rsv1[4];
};
static_assert(sizeof(Mt76Connac2McuUniTxd) == 48, "Connac2 MCU UNI TXD must be 48 bytes");

struct __attribute__((packed, aligned(4))) Mt76Connac2McuRxd {
    uint32_t rxd[6];
    uint16_t len;
    uint16_t pkt_type_id;
    uint8_t eid;
    uint8_t seq;
    uint8_t option;
    uint8_t rsv;
    uint8_t ext_eid;
    uint8_t rsv1[2];
    uint8_t s2d_index;
};
static_assert(sizeof(Mt76Connac2McuRxd) == 36, "Connac2 MCU RXD must be 36 bytes");

struct __attribute__((packed)) Mt76ConnacMcuUniEvent {
    uint8_t cid;
    uint8_t pad[3];
    uint32_t status;
};

// Patch binary structures (big-endian on disk)
struct __attribute__((packed)) Mt76Connac2PatchHdr {
    char build_date[16];
    char platform[4];
    uint32_t hw_sw_ver;
    uint32_t patch_ver;
    uint16_t checksum;
    uint16_t rsv;
    struct {
        uint32_t patch_ver;
        uint32_t subsys;
        uint32_t feature;
        uint32_t n_region;
        uint32_t crc;
        uint32_t rsv[11];
    } desc;
};
static_assert(sizeof(Mt76Connac2PatchHdr) == 96, "Connac2 patch header must be 96 bytes");

struct __attribute__((packed)) Mt76Connac2PatchSec {
    uint32_t type;
    uint32_t offs;
    uint32_t size;
    union {
        uint32_t spec[13];
        struct {
            uint32_t addr;
            uint32_t len;
            uint32_t sec_key_idx;
            uint32_t align_len;
            uint32_t rsv[9];
        } info;
    };
};
static_assert(sizeof(Mt76Connac2PatchSec) == 64, "Connac2 patch section must be 64 bytes");

// WM RAM firmware trailer & region structures (little-endian on disk)
struct __attribute__((packed)) Mt76Connac2FwTrailer {
    uint8_t chip_id;
    uint8_t eco_code;
    uint8_t n_region;
    uint8_t format_ver;
    uint8_t format_flag;
    uint8_t rsv[2];
    char fw_ver[10];
    char build_date[15];
    uint32_t crc;
};
static_assert(sizeof(Mt76Connac2FwTrailer) == 36, "Connac2 FW trailer must be 36 bytes");

struct __attribute__((packed)) Mt76Connac2FwRegion {
    uint32_t decomp_crc;
    uint32_t decomp_len;
    uint32_t decomp_blk_sz;
    uint8_t rsv[4];
    uint32_t addr;
    uint32_t len;
    uint8_t feature_set;
    uint8_t type;
    uint8_t rsv1[14];
};
static_assert(sizeof(Mt76Connac2FwRegion) == 40, "Connac2 FW region must be 40 bytes");

// NIC Capability TLV IDs
enum NicCapTlv : uint32_t {
    MT_NIC_CAP_MAC_ADDR = 7,
    MT_NIC_CAP_PHY = 8,
    MT_NIC_CAP_6G = 25,
    MT_NIC_CAP_CHIP_CAP = 0x20,
};

// Connection & BSS/STA TLV constants (matching Linux mt76_connac_mcu.h)
static constexpr uint32_t STA_TYPE_STA = MTK_BIT(0);
static constexpr uint32_t STA_TYPE_AP = MTK_BIT(1);
static constexpr uint32_t STA_TYPE_BC = MTK_BIT(5);
static constexpr uint32_t NETWORK_INFRA = MTK_BIT(16);
static constexpr uint32_t CONNECTION_INFRA_STA = STA_TYPE_STA | NETWORK_INFRA;
static constexpr uint32_t CONNECTION_INFRA_AP = STA_TYPE_AP | NETWORK_INFRA;
static constexpr uint32_t CONNECTION_INFRA_BC = STA_TYPE_BC | NETWORK_INFRA;
static constexpr uint8_t CONN_STATE_DISCONNECT = 0;
static constexpr uint8_t CONN_STATE_CONNECT = 1;
static constexpr uint8_t CONN_STATE_PORT_SECURE = 2;

static constexpr uint16_t DEV_INFO_ACTIVE = 0;
static constexpr uint16_t UNI_BSS_INFO_BASIC = 0;
static constexpr uint16_t UNI_BSS_INFO_RLM = 2;
static constexpr uint16_t UNI_BSS_INFO_QBSS = 15;
static constexpr uint16_t UNI_BSS_INFO_PS = 21;
static constexpr uint16_t UNI_BSS_INFO_BCNFT = 22;

static constexpr uint16_t STA_REC_BASIC = 0x00;
static constexpr uint16_t STA_REC_RA = 0x01;
static constexpr uint16_t STA_REC_STATE = 0x07;
static constexpr uint16_t STA_REC_HT = 0x09;
static constexpr uint16_t STA_REC_VHT = 0x0a;
static constexpr uint16_t STA_REC_KEY_V2 = 0x0e;
static constexpr uint16_t STA_REC_PHY = 0x15;
static constexpr uint16_t STA_REC_HE = 0x17;
static constexpr uint16_t STA_REC_REMOVE = 0x1e;

enum McuCipherType : uint8_t {
    MCU_CIPHER_NONE = 0,
    MCU_CIPHER_WEP40 = 1,
    MCU_CIPHER_WEP104 = 2,
    MCU_CIPHER_WEP128 = 3,
    MCU_CIPHER_TKIP = 4,
    MCU_CIPHER_AES_CCMP = 5,
    MCU_CIPHER_CCMP_256 = 6,
    MCU_CIPHER_GCMP = 7,
    MCU_CIPHER_GCMP_256 = 8,
    MCU_CIPHER_BIP_CMAC_128 = 10,
};

enum ChanBandwidth : uint8_t {
    CMD_CBW_20MHZ = 0,
    CMD_CBW_40MHZ = 1,
    CMD_CBW_80MHZ = 2,
    CMD_CBW_160MHZ = 3,
};

enum ChSwitchReason : uint8_t {
    CH_SWITCH_NORMAL = 0,
    CH_SWITCH_SCAN = 3,
    CH_SWITCH_SCAN_BYPASS_DPD = 9,
};

// PHY mode bits in BSS basic TLV
static constexpr uint8_t PHY_MODE_A = MTK_BIT(0);
static constexpr uint8_t PHY_MODE_B = MTK_BIT(1);
static constexpr uint8_t PHY_MODE_G = MTK_BIT(2);
static constexpr uint8_t PHY_MODE_GN = MTK_BIT(3);
static constexpr uint8_t PHY_MODE_AN = MTK_BIT(4);
static constexpr uint8_t PHY_MODE_AC = MTK_BIT(5);
static constexpr uint8_t PHY_MODE_AX_24G = MTK_BIT(6);
static constexpr uint8_t PHY_MODE_AX_5G = MTK_BIT(7);
static constexpr uint8_t PHY_MODE_AX_6G = MTK_BIT(0);

// DEV_INFO_UPDATE request
struct __attribute__((packed)) UniDevInfoReq {
    uint8_t omac_idx;
    uint8_t band_idx;
    uint16_t pad;
    uint16_t tag;
    uint16_t len;
    uint8_t active;
    uint8_t link_idx;
    uint8_t omac_addr[6];
};

// UNI_BSS_INFO_BASIC TLV
struct __attribute__((packed)) BssBasicTlv {
    uint16_t tag;
    uint16_t len;
    uint8_t active;
    uint8_t omac_idx;
    uint8_t hw_bss_idx;
    uint8_t band_idx;
    uint32_t conn_type;
    uint8_t conn_state;
    uint8_t wmm_idx;
    uint8_t bssid[6];
    uint16_t bmc_tx_wlan_idx;
    uint16_t bcn_interval;
    uint8_t dtim_period;
    uint8_t phymode;
    uint16_t sta_idx;
    uint16_t nonht_basic_phy;
    uint8_t phymode_ext;
    uint8_t link_idx;
};

struct __attribute__((packed)) BssQosTlv {
    uint16_t tag;
    uint16_t len;
    uint8_t qos;
    uint8_t pad[3];
};

struct __attribute__((packed)) BssRlmTlv {
    uint16_t tag;
    uint16_t len;
    uint8_t control_channel;
    uint8_t center_chan;
    uint8_t center_chan2;
    uint8_t bw;
    uint8_t tx_streams;
    uint8_t rx_streams;
    uint8_t short_st;
    uint8_t ht_op_info;
    uint8_t sco;
    uint8_t band;
    uint8_t pad[2];
};

// STA_REC_UPDATE header + TLVs
struct __attribute__((packed)) StaReqHdr {
    uint8_t bss_idx;
    uint8_t wlan_idx_lo;
    uint16_t tlv_num;
    uint8_t is_tlv_append;
    uint8_t muar_idx;
    uint8_t wlan_idx_hi;
    uint8_t rsv;
};

struct __attribute__((packed)) StaRecBasicTlv {
    uint16_t tag;
    uint16_t len;
    uint32_t conn_type;
    uint8_t conn_state;
    uint8_t qos;
    uint16_t aid;
    uint8_t peer_addr[6];
    uint16_t extra_info; // BIT(0)=VER, BIT(1)=NEW
};

struct __attribute__((packed)) StaRecStateTlv {
    uint16_t tag;
    uint16_t len;
    uint32_t flags;
    uint8_t state;
    uint8_t vht_opmode;
    uint8_t action;
    uint8_t rsv;
};

struct __attribute__((packed)) StaRecPhyTlv {
    uint16_t tag;
    uint16_t len;
    uint16_t basic_rate;
    uint8_t phy_type;
    uint8_t ampdu;
    uint8_t rts_policy;
    uint8_t rcpi;
    uint8_t max_ampdu_len;
    uint8_t rsv;
};

struct __attribute__((packed)) SecKeyEntry {
    uint8_t cipher_id;
    uint8_t cipher_len;
    uint8_t key_id;
    uint8_t key_len;
    uint8_t key[32];
};

struct __attribute__((packed)) StaRecSecTlv {
    uint16_t tag;
    uint16_t len;
    uint8_t add;
    uint8_t n_cipher;
    uint8_t rsv[2];
    SecKeyEntry key[2];
};

// Channel Switch / Set Rx Path request (exact 76 bytes matching mt7921_mcu_set_chan_info)
struct __attribute__((packed)) Mt7902ChanSwitchReq {
    uint8_t control_ch;
    uint8_t center_ch;
    uint8_t bw;
    uint8_t tx_streams_num;
    uint8_t rx_streams;
    uint8_t switch_reason;
    uint8_t band_idx;
    uint8_t center_ch2;
    uint16_t cac_case;
    uint8_t channel_band;
    uint8_t rsv0;
    uint32_t outband_freq;
    uint8_t txpower_drop;
    uint8_t ap_bw;
    uint8_t ap_center_ch;
    uint8_t rsv1[57];
};
static_assert(sizeof(Mt7902ChanSwitchReq) == 76, "Channel switch req must be 76 bytes");

// Hardware Scan Request (MCU_CE_CMD_START_HW_SCAN)
static constexpr size_t MT76_CONNAC_SCAN_IE_LEN = 600;
static constexpr uint8_t SCAN_FUNC_RANDOM_MAC = MTK_BIT(0);
static constexpr uint8_t SCAN_FUNC_SPLIT_SCAN = MTK_BIT(5);

struct __attribute__((packed)) ScanSsidEntry {
    uint32_t ssid_len;
    uint8_t ssid[32];
};

struct __attribute__((packed)) ScanChannelEntry {
    uint8_t band; // 1: 2.4GHz, 2: 5GHz, 3: 6GHz
    uint8_t channel_num;
};

struct __attribute__((packed)) Mt7902HwScanReq {
    uint8_t seq_num;
    uint8_t bss_idx;
    uint8_t scan_type;
    uint8_t ssid_type;
    uint8_t ssids_num;
    uint8_t probe_req_num;
    uint8_t scan_func;
    uint8_t version;
    ScanSsidEntry ssids[4];
    uint16_t probe_delay_time;
    uint16_t channel_dwell_time;
    uint16_t timeout_value;
    uint8_t channel_type;
    uint8_t channels_num;
    ScanChannelEntry channels[32];
    uint16_t ies_len;
    uint8_t ies[MT76_CONNAC_SCAN_IE_LEN];
    uint8_t ext_channels_num;
    uint8_t ext_ssids_num;
    uint16_t channel_min_dwell_time;
    ScanChannelEntry ext_channels[32];
    ScanSsidEntry ext_ssids[6];
    uint8_t bssid[6];
    uint8_t random_mac[6];
    uint8_t pad[63];
    uint8_t ssid_type_ext;
};

} // namespace mt7902

#endif // MT7902_MCU_TYPES_HPP
