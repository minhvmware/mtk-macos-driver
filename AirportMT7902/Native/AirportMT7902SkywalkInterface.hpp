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
    virtual bool init(IOService *provider) MTK_SKYWALK_OVERRIDE;
#ifdef AIRPORT_WCL
    IOWorkLoop *getWorkLoop() const override;
#endif

    // Event notifications from AirportMT7902 (IStaEventListener)
    void notifyDriverAvailable(bool ready);
    void notifyScanDone();
    void notifyBssDiscovered(const mt7902::ScannedBss &bss);
    void notifyAssocDone(bool success, uint16_t statusCode);
    void notifyLinkChanged(bool up);
    void notifyDeauth(uint16_t reasonCode, bool isDisassoc);
    void notifyBeacon(const mt7902::ScannedBss &bss);

    // Implemented core Apple80211 / Skywalk protocol virtuals
    virtual IOReturn getSSID(apple80211_ssid_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getAUTH_TYPE(apple80211_authtype_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getCHANNEL(apple80211_channel_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getPOWERSAVE(apple80211_powersave_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getTXPOWER(apple80211_txpower_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getRATE(apple80211_rate_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getBSSID(apple80211_bssid_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getSCAN_RESULT(apple80211_scan_result *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getSTATE(apple80211_state_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getPHY_MODE(apple80211_phymode_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getOP_MODE(apple80211_opmode_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getRSSI(apple80211_rssi_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getNOISE(apple80211_noise_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getSUPPORTED_CHANNELS(apple80211_sup_channel_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *d) override {
        return getSUPPORTED_CHANNELS(d);
    }
    virtual IOReturn getLOCALE(apple80211_locale_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getDEAUTH(apple80211_deauth_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getRATE_SET(apple80211_rate_set_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getRSN_IE(apple80211_rsn_ie_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getAP_IE_LIST(apple80211_ap_ie_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getASSOCIATION_STATUS(apple80211_assoc_status_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getMCS(apple80211_mcs_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getMCS_INDEX_SET(apple80211_mcs_index_set_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getVHT_MCS_INDEX_SET(apple80211_vht_mcs_index_set_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getMCS_VHT(apple80211_mcs_vht_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getNSS(apple80211_nss_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getHW_ADDR(apple80211_hw_mac_address *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getCURRENT_NETWORK(apple80211_scan_result *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getLINK_CHANGED_EVENT_DATA(apple80211_link_changed_event_data *) MTK_SKYWALK_OVERRIDE;

    virtual IOReturn setAUTH_TYPE(apple80211_authtype_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn setCIPHER_KEY(apple80211_key *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn setSCAN_REQ(apple80211_scan_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn setASSOCIATE(apple80211_assoc_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn setDISASSOCIATE(apple80211_disassoc_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn setDEAUTH(apple80211_deauth_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn setRSN_IE(apple80211_rsn_ie_data *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn setSCANCACHE_CLEAR(void *) MTK_SKYWALK_OVERRIDE;

    // Complete set of remaining IO80211InfraProtocol pure-virtual stubs (Sonoma + Sequoia + Tahoe)
    virtual IOReturn getDTIM_INT(apple80211_dtim_int_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getSTATION_LIST(apple80211_sta_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getSTATS(apple80211_stats_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getGUARD_INTERVAL(apple80211_guard_interval_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn getWOW_PARAMETERS(apple80211_wow_parameter_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getWOW_ENABLED(apple80211_state_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getPID_LOCK(apple80211_state_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getSTA_IE_LIST(apple80211_sta_ie_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getSTA_STATS(apple80211_sta_stats_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getBT_COEX_FLAGS(apple80211_state_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getRSSI_BOUNDS(apple80211_rssi_bounds_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getPOWER_DEBUG_INFO(apple80211_power_debug_info *) override { return kIOReturnUnsupported; }
    virtual IOReturn getHT_CAPABILITY(apple80211_ht_capability *) override { return kIOReturnUnsupported; }
    virtual IOReturn getEXTENDED_STATS(apple80211_extended_stats *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getBEACON_PERIOD(apple80211_beacon_period_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getGAS_RESULTS(apple80211_gas_result_t *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getVHT_CAPABILITY(apple80211_vht_capability *) override { return kIOReturnUnsupported; }
    virtual IOReturn getBGSCAN_CACHE_RESULTS(apple80211_bgscan_cached_network_data_list *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getROAM_PROFILE(apple80211_roam_profile_band_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getCHIP_COUNTER_STATS(apple80211_chip_stats *) override { return kIOReturnUnsupported; }
    virtual IOReturn getDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *) override { return kIOReturnUnsupported; }
    virtual IOReturn getLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting *) override { return kIOReturnUnsupported; }
    virtual IOReturn getCOUNTRY_CHANNELS(apple80211_country_channel_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn getPRIVATE_MAC(apple80211_private_mac_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn getRANGING_ENABLE(apple80211_ranging_enable_request_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn getRANGING_START(apple80211_ranging_start_request_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn getAWDL_RSDB_CAPS(apple80211_rsdb_capability *) override { return kIOReturnUnsupported; }
    virtual IOReturn getTKO_PARAMS(apple80211_tko_params *) override { return kIOReturnUnsupported; }
    virtual IOReturn getTKO_DUMP(apple80211_tko_dump *) override { return kIOReturnUnsupported; }
    virtual IOReturn getBTCOEX_PROFILE(apple80211_btcoex_profile *) override { return kIOReturnUnsupported; }
    virtual IOReturn getBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn getTRAP_INFO(apple80211_trap_info_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn getTHERMAL_INDEX(apple80211_thermal_index_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn getBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *) override { return kIOReturnUnsupported; }
    virtual IOReturn getPOWER_BUDGET(apple80211_power_budget_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn getOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn getRANGING_CAPS(apple80211_ranging_capabilities_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn getSUPPRESS_SCANS(apple80211_suppress_scans_t *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getHOST_AP_MODE_HIDDEN(apple80211_host_ap_mode_hidden_t *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getLQM_CONFIG(apple80211_lqm_config_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn getTRAP_CRASHTRACER_MINI_DUMP(apple80211_trap_mini_dump_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn getHE_CAPABILITY(apple80211_he_capability *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getSOFTAP_PARAMS(apple80211_softap_params *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getCHIP_POWER_RANGE(apple80211_chip_power_limit *) override { return kIOReturnUnsupported; }
    virtual IOReturn getSOFTAP_STATS(apple80211_softap_stats *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getHE_MCS_INDEX_SET(apple80211_he_mcs_index_set_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getCHIP_DIAGS(appl80211_chip_diags_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn getHP2P_CTRL(apple80211_hp2p_ctrl *) override { return kIOReturnUnsupported; }
    virtual IOReturn getREQUEST_BSS_BLACKLIST(void *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getASSOC_READY_STATUS(apple80211_assoc_ready *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getTXRX_CHAIN_INFO(apple80211_txrx_chain_info *) override { return kIOReturnUnsupported; }
    virtual IOReturn getMIMO_STATUS(apple80211_mimo_status *) override { return kIOReturnUnsupported; }
    virtual IOReturn getCUR_PMK(apple80211_pmk *) override { return kIOReturnUnsupported; }
    virtual IOReturn getDYNSAR_DETAIL(apple80211_dynsar_detail *) override { return kIOReturnUnsupported; }
    virtual IOReturn getRANDOMISATION_STATUS(apple80211_mac_randomisation_status *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getCOUNTRY_CHANNELS_INFO(apple80211_channels_info *) override { return kIOReturnUnsupported; }
    virtual IOReturn getLQM_SUMMARY(apple80211_lqm_summary *) override { return kIOReturnUnsupported; }
    virtual IOReturn getCOLOCATED_NETWORK_SCOPE_ID(apple80211_colocated_network_scope_id *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getBEACON_SCAN_CACHE_REQ(apple80211_scan_result *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getSLOW_WIFI_FEATURE_ENABLED(apple80211_slow_wifi_feature_enabled *) override { return kIOReturnUnsupported; }
    virtual IOReturn getCCA(apple80211_interface_cca_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getRX_RATE(apple80211_rate_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getTIMESYNC_INFO(apple80211_timesync_info *) override { return kIOReturnUnsupported; }
    virtual IOReturn getSENSING_DATA(apple80211_sensing_data_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn getCOUNTRY_BAND_SUPPORT(apple80211_country_band_support *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels *) override { return kIOReturnUnsupported; }
    virtual IOReturn getWCL_GET_TX_BLANKING_STATUS(unsigned int *) override { return kIOReturnUnsupported; }
    virtual IOReturn getSSID_TRANSITION_SUPPORT(apple80211_ssid_transition_feature_enabled *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getWCL_VALID_CHANNEL_COUNT(unsigned long *) MTK_SKYWALK_OVERRIDE;
    virtual IOReturn getWCL_P2P_STATUS_FOR_SCAN(p2pStatusForScan *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getP2P_STEERING_METRIC(apple80211_p2p_steering_metrics *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn getRSN_XE(apple80211_rsn_xe_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn getSIB_COEX_STATUS(apple80211_sib_coex_status *) override { return kIOReturnUnsupported; }
    virtual IOReturn getWCL_LOW_LATENCY_INFO_STATS(apple80211_wcl_low_latency_stats *) override { return kIOReturnUnsupported; }
    virtual IOReturn getWCL_BGSCAN_CACHE_RESULT(apple80211_bgscan_cached_network_data_list *) override { return kIOReturnUnsupported; }
    virtual IOReturn getWIFI_NOISE_PER_ANT(apple80211_noise_per_ant_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn getBLOCKED_BANDS(apple80211_blocked_bands *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }

    virtual IOReturn setSSID(apple80211_ssid_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setCHANNEL(apple80211_channel_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setPOWERSAVE(apple80211_powersave_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setTXPOWER(apple80211_txpower_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setRATE(apple80211_rate_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setIBSS_MODE(apple80211_network_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setHOST_AP_MODE(apple80211_network_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setAP_MODE(apple80211_apmode_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setTX_ANTENNA(void *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setANTENNA_DIVERSITY(void *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setBACKGROUND_SCAN(apple80211_bgscan_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setWOW_PARAMETERS(apple80211_wow_parameter_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setWOW_ENABLED(apple80211_state_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setPID_LOCK(apple80211_state_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setSTA_AUTHORIZE(apple80211_sta_authorize_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setSTA_DISASSOCIATE(apple80211_sta_disassoc_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setSTA_DEAUTH(apple80211_sta_disassoc_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setRSN_CONF(apple80211_rsn_conf_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setIE(apple80211_ie_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWOW_TEST(apple80211_wow_test_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setBT_COEX_FLAGS(apple80211_state_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setROAM(apple80211_sta_roam_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setHT_CAPABILITY(apple80211_ht_capability *) override { return kIOReturnUnsupported; }
    virtual IOReturn setAWDL_FORCED_ROAM_CONFIG(apple80211_awdl_forced_roam_config *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setOFFLOAD_ARP(apple80211_offload_arp_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setOFFLOAD_NDP(apple80211_offload_ndp_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setOFFLOAD_SCAN(apple80211_offload_scan_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setGAS_REQ(apple80211_gas_query_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setGAS_START(apple80211_gas_query_t *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setGAS_SET_PEER(apple80211_gas_peer_t *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setVHT_CAPABILITY(apple80211_vht_capability *) override { return kIOReturnUnsupported; }
    virtual IOReturn setROAM_PROFILE(apple80211_roam_profile_band_data *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setAWDL_ENABLE_ROAMING(void *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *) override { return kIOReturnUnsupported; }
    virtual IOReturn setLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting *) override { return kIOReturnUnsupported; }
    virtual IOReturn setPRIVATE_MAC(apple80211_private_mac_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setRESET_CHIP(apple80211_reset_command *) override { return kIOReturnUnsupported; }
    virtual IOReturn setCRASH(apple80211_crash_command *) override { return kIOReturnUnsupported; }
    virtual IOReturn setRANGING_ENABLE(apple80211_ranging_enable_request_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setRANGING_START(apple80211_ranging_start_request_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setRANGING_AUTHENTICATE(apple80211_ranging_authenticate_request_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setTKO_PARAMS(apple80211_tko_params *) override { return kIOReturnUnsupported; }
    virtual IOReturn setBTCOEX_PROFILE(apple80211_btcoex_profile *) override { return kIOReturnUnsupported; }
    virtual IOReturn setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setTHERMAL_INDEX(apple80211_thermal_index_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *) override { return kIOReturnUnsupported; }
    virtual IOReturn setPOWER_BUDGET(apple80211_power_budget_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setSUPPRESS_SCANS(apple80211_suppress_scans_t *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setHOST_AP_MODE_HIDDEN(apple80211_host_ap_mode_hidden_t *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setLQM_CONFIG(apple80211_lqm_config_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setSOFTAP_PARAMS(apple80211_softap_params *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setSOFTAP_TRIGGER_CSA(apple80211_softap_csa_params *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setSOFTAP_WIFI_NETWORK_INFO_IE(apple80211_softap_wifi_network_info *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setBTCOEX_DISABLE_ULOFDMA(unsigned int *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setSCAN_CONTROL(apple80211_scan_control_params *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setSET_MAC_ADDRESS(apple80211_set_mac_address *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setHP2P_CTRL(apple80211_hp2p_ctrl *) override { return kIOReturnUnsupported; }
    virtual IOReturn setABORT_SCAN(apple80211_abort_scan *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setSET_PROPERTY(apple80211_set_property_unserialized_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setROAM_CACHE_UPDATE(apple80211_roam_cache_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setPM_MODE(apple80211_pm_mode *) override { return kIOReturnUnsupported; }
    virtual IOReturn setSET_WIFI_ASSERTION_STATE(apple80211_wifi_assertion_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setREASSOCIATE_WITH_CORECAPTURE(apple80211_capture_debug_info_t *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setLINKDOWN_DEBOUNCE_STATUS(apple80211_linkdown_debounce_status *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setSOFTAP_EXTENDED_CAPABILITIES_IE(apple80211_softap_extended_capabilities_info *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setREALTIME_QOS_MSCS(apple80211_state_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setSENSING_ENABLE(apple80211_sensing_enable_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setSENSING_DISABLE(apple80211_sensing_disable_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setNANPHS_ASSOCIATION(apple80211_nan_link_association_info *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setNANPHS_TERMINATED(apple80211_nan_link_association_info *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn set6G_MODE(apple80211_6G_mode *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_REASSOC(apple80211_reassoc *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_ROAM_PROFILE_CONFIGV1(apple80211_roam_profile_configV1 *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_ROAM_USER_CACHE(apple80211_user_roam_cache *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_SET_MULTI_AP_ENV(apple80211_set_multi_ap_env *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_GARP_MODE(apple80211_wcl_garp_mode *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_TRIGGER_CC(triggerCC *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_ASSOCIATE(apple80211_assoc_candidates *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_PROTECT_IP(apple80211_wcl_protect_ip_mode *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_LINK_UP_DONE(void *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_VOLUNTARY_NETWORK_DISCONNECT(apple80211_wcl_voluntary_network_disconnect *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setSLOW_WIFI_RECOVERY(void *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setRSN_XE(apple80211_rsn_xe_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_ULOFDMA_STATE(apple80211_wcl_ulofdma_state *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_ACTION_FRAME(apple80211_wcl_action_frame *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_REAL_TIME_POLICY(apple80211_wcl_real_time_policy *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setGAS_ABORT(void *) override { return kIOReturnUnsupported; }
    virtual IOReturn setOS_FEATURE_FLAGS(apple80211_feature_flags *) override { return kIOReturnUnsupported; }
    virtual IOReturn setDHCP_RENEWAL_DATA(apple80211_dhcp_renewal_data *) override { return kIOReturnUnsupported; }
    virtual IOReturn setMOVING_NETWORK(apple80211_network_flags *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }
    virtual IOReturn setBATTERY_POWERSAVE_CONFIG(apple80211_battery_ps_config *) override { return kIOReturnUnsupported; }
    virtual IOReturn setMIMO_CONFIG(apple80211_mimo_config *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_CONFIG_BG_MOTIONPROFILE(apple80211_bg_motion_profile *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_CONFIG_BG_NETWORK(apple80211_bg_network *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_CONFIG_BGSCAN(apple80211_bg_scan *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_CONFIG_BG_PARAMS(apple80211_bg_params *) override { return kIOReturnUnsupported; }
    virtual IOReturn setBLOCKED_BANDS(apple80211_blocked_bands *) MTK_SKYWALK_OVERRIDE { return kIOReturnUnsupported; }

#ifdef AIRPORT_WCL
    IOReturn getCHANNELS_INFO(apple80211_channels_info *) override;
    IOReturn getWCL_CHANNELS_INFO(apple80211ChannelInfo *) override;
    IOReturn getWCL_BSS_INFO(apple80211_beacon_msg *) override;
    IOReturn getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *) override;
    IOReturn getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *) override;
    IOReturn getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *) override;
    IOReturn getBEACON_INFO(apple80211_beacon_info_t *) override;

    IOReturn setWCL_SCAN_REQ(apple80211ScanRequest *) override;
    IOReturn setWCL_SCAN_ABORT(void *) override;
    IOReturn setWCL_ASSOCIATE(apple80211AssocCandidates *) override;
    IOReturn setWCL_LEAVE_NETWORK(apple80211_leave_network *) override;
    IOReturn setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *) override;
    IOReturn setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *) override { return kIOReturnSuccess; }
    IOReturn setWCL_UPDATE_FAST_LANE(apple80211_fastlane *) override { return kIOReturnSuccess; }
    IOReturn setCLEAR_PMKSA_CACHE(void *) override { return kIOReturnSuccess; }

#if __IO80211_TARGET == __MAC_15_2
    IOReturn setWCL_JOIN_ABORT(void *) override;
    IOReturn setMacAddress(ether_addr &) override;
    IOReturn getWIFI_BT_5G_POLICY(apple80211_wifi_bt_5g_policy_t *) override { return kIOReturnUnsupported; }
    IOReturn setWIFI_BT_5G_POLICY(apple80211_wifi_bt_5g_policy_t *) override { return kIOReturnUnsupported; }
#else
    IOReturn setWCL_JOIN_ABORT(apple80211_wcl_abort_join *) override;
    IOReturn setMacAddress(mloAddrArray &) override;
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
#else
    // Sonoma non-WCL versions of shared methods
    virtual IOReturn getCHANNELS_INFO(apple80211_channels_info *) override { return kIOReturnUnsupported; }
    virtual IOReturn getWCL_CHANNELS_INFO(apple80211ChannelInfo *) override { return kIOReturnUnsupported; }
    virtual IOReturn getWCL_BSS_INFO(apple80211_beacon_msg *) override { return kIOReturnUnsupported; }
    virtual IOReturn getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *) override { return kIOReturnUnsupported; }
    virtual IOReturn getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *) override { return kIOReturnUnsupported; }
    virtual IOReturn getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *) override { return kIOReturnUnsupported; }
    virtual IOReturn getBEACON_INFO(apple80211_beacon_info_t *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_SCAN_REQ(apple80211ScanRequest *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_SCAN_ABORT(void *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_LEAVE_NETWORK(apple80211_leave_network *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_JOIN_ABORT(void *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *) override { return kIOReturnUnsupported; }
    virtual IOReturn setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *) override { return kIOReturnUnsupported; }
#endif

private:
#ifdef AIRPORT_WCL
    void postWclMessage(uint32_t msgId, void *data, size_t len);
    void populateWclBeaconBuffer(const mt7902::ScannedBss &bss, uint8_t *buf, size_t &outTotalLen);
    void finishWclJoin(uint16_t status, uint16_t reason, IOReturn driverStatus);
#endif
    AirportMT7902 *mController = nullptr;
    mt7902::MT7902Sta *mSta = nullptr;
#ifdef AIRPORT_WCL
    uint8_t mWclBeaconMsg[sizeof(mt7902::wcl::BeaconMetadata) + 0x800] = {};
    uint8_t mWclBeaconInfo[4 + 0x800] = {};
    uint8_t mWclJoinBssid[6] = {};
    uint32_t mWclAssociatedBeacons = 0;
    bool mWclScanPending = false;
    bool mWclJoinPending = false;
    bool mWclAssocNotified = false;
    bool mWclFirstBeaconSent = false;
    bool mWclPortAuthorized = false;
    bool mWclBssValid = false;
    bool mWclLinkIndicated = false;
#endif
};

#endif // KERNEL && IO80211FAMILY_V2
#endif // AIRPORT_MT7902_SKYWALK_INTERFACE_HPP
