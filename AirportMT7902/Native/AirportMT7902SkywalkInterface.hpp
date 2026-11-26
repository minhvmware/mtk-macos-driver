// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// IO80211InfraProtocol subclass for macOS 14 Sonoma, 15.2+ Sequoia (WCL), and 26 Tahoe (WCL)

#ifndef AIRPORT_MT7902_SKYWALK_INTERFACE_HPP
#define AIRPORT_MT7902_SKYWALK_INTERFACE_HPP

#if defined(KERNEL) && defined(IO80211FAMILY_V2)

#include <Airport/Apple80211.h>
#include "AirportMT7902WCL.hpp"
#include "../Net80211/MT7902Sta.hpp"

#ifdef AIRPORT_WCL
#define MTK_SKYWALK_OVERRIDE
#else
#define MTK_SKYWALK_OVERRIDE override
#endif

class AirportMT7902;

class AirportMT7902SkywalkInterface : public IO80211InfraProtocol {
    OSDeclareDefaultStructors(AirportMT7902SkywalkInterface)

public:
    bool initWithController(AirportMT7902 *ctrl, mt7902::MT7902Sta *sta);
    bool init(IOService *provider) MTK_SKYWALK_OVERRIDE;

    // Event notifications from AirportMT7902 (IStaEventListener)
    void notifyScanDone();
    void notifyBssDiscovered(const mt7902::ScannedBss &bss);
    void notifyAssocDone(bool success, uint16_t statusCode);
    void notifyLinkChanged(bool up);
    void notifyDeauth(uint16_t reasonCode, bool isDisassoc);
    void notifyBeacon(const mt7902::ScannedBss &bss);

    // Core Apple80211 / Skywalk protocol virtuals (Sonoma + Sequoia/Tahoe)
    IOReturn getSSID(apple80211_ssid_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getAUTH_TYPE(apple80211_authtype_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getCHANNEL(apple80211_channel_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getPOWERSAVE(apple80211_powersave_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getTXPOWER(apple80211_txpower_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getRATE(apple80211_rate_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getBSSID(apple80211_bssid_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getSCAN_RESULT(apple80211_scan_result *) MTK_SKYWALK_OVERRIDE;
    IOReturn getSTATE(apple80211_state_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getPHY_MODE(apple80211_phymode_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getOP_MODE(apple80211_opmode_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getRSSI(apple80211_rssi_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getNOISE(apple80211_noise_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getSUPPORTED_CHANNELS(apple80211_sup_channel_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *d) MTK_SKYWALK_OVERRIDE {
        return getSUPPORTED_CHANNELS(d);
    }
    IOReturn getLOCALE(apple80211_locale_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getDEAUTH(apple80211_deauth_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getRATE_SET(apple80211_rate_set_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getRSN_IE(apple80211_rsn_ie_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getAP_IE_LIST(apple80211_ap_ie_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getASSOCIATION_STATUS(apple80211_assoc_status_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getMCS(apple80211_mcs_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getMCS_INDEX_SET(apple80211_mcs_index_set_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getVHT_MCS_INDEX_SET(apple80211_vht_mcs_index_set_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getMCS_VHT(apple80211_mcs_vht_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getNSS(apple80211_nss_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn getHW_ADDR(apple80211_hw_mac_address *) MTK_SKYWALK_OVERRIDE;
    IOReturn getCURRENT_NETWORK(apple80211_scan_result *) MTK_SKYWALK_OVERRIDE;
    IOReturn getLINK_CHANGED_EVENT_DATA(apple80211_link_changed_event_data *) MTK_SKYWALK_OVERRIDE;

    IOReturn setAUTH_TYPE(apple80211_authtype_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn setCIPHER_KEY(apple80211_key *) MTK_SKYWALK_OVERRIDE;
    IOReturn setSCAN_REQ(apple80211_scan_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn setASSOCIATE(apple80211_assoc_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn setDISASSOCIATE(apple80211_disassoc_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn setDEAUTH(apple80211_deauth_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn setRSN_IE(apple80211_rsn_ie_data *) MTK_SKYWALK_OVERRIDE;
    IOReturn setSCANCACHE_CLEAR(void *) MTK_SKYWALK_OVERRIDE;

#ifdef AIRPORT_WCL
    // macOS 15.2+ Sequoia & macOS 26 Tahoe native WCL pure-virtual overrides
    IOReturn getCHANNELS_INFO(apple80211_channels_info *) override;
    IOReturn getWCL_CHANNELS_INFO(apple80211ChannelInfo *) override;
    IOReturn getWCL_BSS_INFO(apple80211_beacon_msg *) override;
    IOReturn getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *) override;
    IOReturn getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *) override;
    IOReturn getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *) override { return kIOReturnUnsupported; }
    IOReturn getBEACON_INFO(apple80211_beacon_info_t *) override;

    IOReturn setWCL_SCAN_REQ(apple80211ScanRequest *) override;
    IOReturn setWCL_SCAN_ABORT(void *) override;
    IOReturn setWCL_ASSOCIATE(apple80211AssocCandidates *) override;
    IOReturn setWCL_LEAVE_NETWORK(apple80211_leave_network *) override;
    IOReturn setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *) override { return kIOReturnSuccess; }
    IOReturn setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *) override { return kIOReturnSuccess; }
    IOReturn setWCL_UPDATE_FAST_LANE(apple80211_fastlane *) override { return kIOReturnSuccess; }
    IOReturn setCLEAR_PMKSA_CACHE(void *) override { return kIOReturnSuccess; }

#if __IO80211_TARGET == __MAC_15_2
    IOReturn setWCL_JOIN_ABORT(void *) override;
    IOReturn setMacAddress(ether_addr &) override { return kIOReturnSuccess; }
    IOReturn getWIFI_BT_5G_POLICY(apple80211_wifi_bt_5g_policy_t *) override { return kIOReturnUnsupported; }
    IOReturn setWIFI_BT_5G_POLICY(apple80211_wifi_bt_5g_policy_t *) override { return kIOReturnUnsupported; }
#else
    IOReturn setWCL_JOIN_ABORT(apple80211_wcl_abort_join *) override;
    IOReturn setMacAddress(mloAddrArray &) override { return kIOReturnSuccess; }
    IOReturn getSMARTCCA_OPMODE(apple80211_smartcca_opmode *) override { return kIOReturnUnsupported; }
    IOReturn getLQM_STATISTICS(apple80211_lqm_statistics *) override { return kIOReturnUnsupported; }
    IOReturn getDEVICE_ORIENTATION(apple80211_device_orientation *) override { return kIOReturnUnsupported; }
    IOReturn getACCESSORY_STATE(apple80211_device_accessory_info *) override { return kIOReturnUnsupported; }
    IOReturn getP2P_DEVICE_CAPABILITY(apple80211_p2p_device_capability *) override { return kIOReturnUnsupported; }
    IOReturn getPOWERTABLE_VERSION(apple80211_powertable_version_data *) override { return kIOReturnUnsupported; }
    IOReturn setBTCOEX_EXT_PROFILE(apple80211_btcoex_ext_profile *) override { return kIOReturnUnsupported; }
    IOReturn setDEVICE_ORIENTATION(apple80211_device_orientation *) override { return kIOReturnUnsupported; }
    IOReturn setACCESSORY_STATE(apple80211_device_accessory_info *) override { return kIOReturnUnsupported; }
    IOReturn setOS_ELIGIBILITY(apple80211_os_eligibility *) override { return kIOReturnUnsupported; }
    IOReturn setTX_MODE_CONFIG(apple80211_tx_mode_config *) override { return kIOReturnUnsupported; }
    IOReturn setMITIGATE_INTERFERENCE(apple80211_mitigate_interference *) override { return kIOReturnUnsupported; }
#endif

    // Remaining WCL optional feature stubs
    IOReturn getBSS_BLACKLIST(bss_blacklist *) override { return kIOReturnUnsupported; }
    IOReturn getHE_COUNTERS(apple80211_he_counters_ctl *) override { return kIOReturnUnsupported; }
    IOReturn getWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *) override { return kIOReturnUnsupported; }
    IOReturn getFW_CLOCK_INFO(apple80211_fw_clock_info *) override { return kIOReturnUnsupported; }
    IOReturn getTIMESYNC_STATS(apple80211_timesync_stats *) override { return kIOReturnUnsupported; }
    IOReturn getSYSTEM_SLEEP_CONFIG(apple80211_system_sleep_config *) override { return kIOReturnUnsupported; }
    IOReturn getROAM_PROFILE(apple80211_roam_profile_all_bands *) override { return kIOReturnUnsupported; }
    IOReturn setROAM_PROFILE(apple80211_roam_profile_all_bands *) override { return kIOReturnUnsupported; }
    IOReturn setDYNAMIC_RSSI_WINDOW_CONFIG(apple80211_dynamic_rssi_window_config *) override { return kIOReturnUnsupported; }
    IOReturn setBSS_BLACKLIST(bss_blacklist *) override { return kIOReturnUnsupported; }
    IOReturn setWCL_LEGACY_ROAM_PROFILE_CONFIG(apple80211_legacy_roam_profile_config *) override { return kIOReturnUnsupported; }
    IOReturn setWCL_ARP_MODE(apple80211_wcl_arp_mode *) override { return kIOReturnUnsupported; }
    IOReturn setWCL_QOS_PARAMS(apple80211_wcl_qos_params *) override { return kIOReturnUnsupported; }
    IOReturn setVOICE_IND_STATE(apple80211_voice_ind_state *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH(apple80211_mws_accessory_power_limit *) override { return kIOReturnUnsupported; }
    IOReturn setPOWER_PROFILE(apple80211_power_profile *) override { return kIOReturnUnsupported; }
    IOReturn setHEARTBEAT(void *) override { return kIOReturnSuccess; }
    IOReturn setINTERFACE_SETTING(apple80211_interface_setting *) override { return kIOReturnUnsupported; }
    IOReturn setBYPASS_TX_POWER_CAP(apple80211_bypass_tx_power_cap *) override { return kIOReturnUnsupported; }
    IOReturn setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params *) override { return kIOReturnUnsupported; }
    IOReturn setIPV4_PARAMS(apple80211_ipv4_params *) override { return kIOReturnUnsupported; }
    IOReturn setIPV6_PARAMS(apple80211_ipv6_params *) override { return kIOReturnUnsupported; }
    IOReturn setWCL_WNM_OPS(apple80211_wcl_wnm_config_t *) override { return kIOReturnUnsupported; }
    IOReturn setWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *) override { return kIOReturnUnsupported; }
    IOReturn setWCL_LIMITED_AGGREGATION(apple80211_limited_aggregation_config *) override { return kIOReturnUnsupported; }
    IOReturn setWCL_BCN_MUTE_CONFIG(apple80211_bcn_mute_config *) override { return kIOReturnUnsupported; }
    IOReturn setEAP_FILTER_CONFIG(apple80211_eap_filter_config *) override { return kIOReturnUnsupported; }
    IOReturn setWOW_LOW_POWER_MODE(apple80211_wow_low_power_mode *) override { return kIOReturnUnsupported; }
    IOReturn setDUAL_POWER_MODE(apple80211_dual_power_mode_params *) override { return kIOReturnUnsupported; }
    IOReturn setWCL_ASSOCIATED_SLEEP(apple80211_associated_sleep_config *) override { return kIOReturnUnsupported; }
    IOReturn setCONGESTION_CTRL_IND(apple80211_congestion_control_indication *) override { return kIOReturnUnsupported; }
    IOReturn setSTAND_ALONE_MODE_STATE(apple80211_standalone_state *) override { return kIOReturnUnsupported; }
    IOReturn setINFRA_ENUMERATED(apple80211_infra_enumerated *) override { return kIOReturnSuccess; }
    IOReturn setLMTPC_CONFIG(apple80211_lmtpc_config *) override { return kIOReturnUnsupported; }
    IOReturn setTRAFFIC_ENG_PARAMS(apple80211_traffic_eng_params *) override { return kIOReturnUnsupported; }
    IOReturn setLE_SCAN_PARAM(apple80211_le_scan_params *) override { return kIOReturnUnsupported; }
    IOReturn setTIMESYNC_GPIO(apple80211_timesync_gpio *) override { return kIOReturnUnsupported; }
    IOReturn setHOST_CLOCK_INFO(apple80211_host_clock_info *) override { return kIOReturnUnsupported; }
    IOReturn setFW_CLOCK_SOURCE(apple80211_fw_clock_source *) override { return kIOReturnUnsupported; }
    IOReturn setTIMESYNC_TX_POLICY(apple80211_timesync_tx_policy *) override { return kIOReturnUnsupported; }
    IOReturn setTIMESYNC_RX_POLICY(apple80211_timesync_rx_policy *) override { return kIOReturnUnsupported; }
    IOReturn setTIMESTAMPING_EN(apple80211_timestamping_en *) override { return kIOReturnUnsupported; }
    IOReturn setWCL_SOI_CONFIG(appl80211_sleep_on_inactivity_config *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_TIME_SHARING_WIFI_ENH(apple80211_mws_time_sharing *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_COEX_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_DISABLE_OCL_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_RFEM_CONFIG_WIFI_ENH(apple80211_mws_rfem_config *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_SCAN_FREQ_WIFI_ENH(apple80211_mws_scan_freq *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_SCAN_FREQ_MODE_WIFI_ENH(apple80211_mws_scan_freq_mode *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_CONDITION_ID_BITMAP_WIFI_ENH(apple80211_mws_condition_id_config *) override { return kIOReturnUnsupported; }
    IOReturn setMWS_ANTENNA_SELECTION_WIFI_ENH(apple80211_mws_antenna_selection *) override { return kIOReturnUnsupported; }
    IOReturn setNDD_REQ(apple80211_ndd_data *) override { return kIOReturnUnsupported; }
    IOReturn setDBRG_ENTROPY(apple80211_drbg_entropy *) override { return kIOReturnUnsupported; }
    IOReturn setSDB_ENABLE(apple80211_sdb_enable *) override { return kIOReturnUnsupported; }
#endif

private:
    AirportMT7902 *mController = nullptr;
    mt7902::MT7902Sta *mSta = nullptr;
    uint8_t mScanIeBuf[mt7902::kMaxRawIeLen] = {};
#ifdef AIRPORT_WCL
    uint8_t mWclBeaconMsg[sizeof(mt7902::wcl::BeaconMetadata) + 0x800] = {};
    bool mWclFirstBeaconSent = false;
#endif
};

#endif // KERNEL && IO80211FAMILY_V2
#endif // AIRPORT_MT7902_SKYWALK_INTERFACE_HPP
