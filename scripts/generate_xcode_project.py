#!/usr/bin/env python3
"""
Deterministic Xcode project & shared scheme generator for MT7902MacOS.xcodeproj.

Generates 5 native macOS kernel extension (.kext) targets linked against Acidanthera MacKernelSDK:
  1. AirportMT7902-Ventura      (__MAC_13_0, IO80211Controller)
  2. AirportMT7902-Sonoma       (__MAC_14_4, IO80211FAMILY_V2)
  3. AirportMT7902-Sequoia      (__MAC_15_2, IO80211FAMILY_V2 + AIRPORT_WCL)
  4. AirportMT7902-Tahoe        (__MAC_26_0, IO80211FAMILY_V2 + AIRPORT_WCL)
  5. MT7902BluetoothFirmware    (IOUSBHostDevice 79xx WMT firmware loader)
"""

import hashlib
from pathlib import Path

MODULE_VERSION = "1.0.0"


def pbx_id(seed: str) -> str:
    return hashlib.sha1(seed.encode("utf-8")).hexdigest()[:24].upper()


WIFI_SOURCES = [
    "AirportMT7902/Firmware/MT7902WifiFwData.cpp",
    "AirportMT7902/HAL/MT7902Dma.cpp",
    "AirportMT7902/HAL/MT7902Mcu.cpp",
    "AirportMT7902/HAL/MT7902Firmware.cpp",
    "AirportMT7902/HAL/MT7902Mac.cpp",
    "AirportMT7902/HAL/MT7902Device.cpp",
    "AirportMT7902/Net80211/MT7902Channels.cpp",
    "AirportMT7902/Net80211/MT7902BeaconParser.cpp",
    "AirportMT7902/Net80211/MT7902Wpa2.cpp",
    "AirportMT7902/Net80211/MT7902Sta.cpp",
    "AirportMT7902/Native/MT7902IOKitBus.cpp",
    "AirportMT7902/Native/MT7902PCIWrapper.cpp",
    "AirportMT7902/Native/AirportMT7902Interface.cpp",
    "AirportMT7902/Native/AirportMT7902ControllerV1.cpp",
    "AirportMT7902/Native/AirportMT7902EthernetInterface.cpp",
    "AirportMT7902/Native/AirportMT7902SkywalkInterface.cpp",
    "AirportMT7902/Native/AirportMT7902ControllerV2.cpp",
]

WIFI_HEADERS = [
    "include/MT7902Common.hpp",
    "AirportMT7902/PrivateSPI.pch",
    "AirportMT7902/Firmware/MT7902FwStore.hpp",
    "AirportMT7902/HAL/MT7902Regs.hpp",
    "AirportMT7902/HAL/MT7902DmaTypes.hpp",
    "AirportMT7902/HAL/MT7902McuTypes.hpp",
    "AirportMT7902/HAL/MT7902Bus.hpp",
    "AirportMT7902/HAL/MT7902Dma.hpp",
    "AirportMT7902/HAL/MT7902Mcu.hpp",
    "AirportMT7902/HAL/MT7902Firmware.hpp",
    "AirportMT7902/HAL/MT7902Mac.hpp",
    "AirportMT7902/HAL/MT7902Device.hpp",
    "AirportMT7902/Net80211/MT7902Channels.hpp",
    "AirportMT7902/Net80211/MT7902BeaconParser.hpp",
    "AirportMT7902/Net80211/MT7902Wpa2.hpp",
    "AirportMT7902/Net80211/MT7902Sta.hpp",
    "AirportMT7902/Native/MT7902IOKitBus.hpp",
    "AirportMT7902/Native/MT7902PCIWrapper.hpp",
    "AirportMT7902/Native/AirportMT7902WCL.hpp",
    "AirportMT7902/Native/AirportMT7902Interface.hpp",
    "AirportMT7902/Native/AirportMT7902ControllerV1.hpp",
    "AirportMT7902/Native/AirportMT7902EthernetInterface.hpp",
    "AirportMT7902/Native/AirportMT7902SkywalkInterface.hpp",
    "AirportMT7902/Native/AirportMT7902ControllerV2.hpp",
]

BT_SOURCES = [
    "MT7902BluetoothFirmware/Firmware/MT7902BtFwData.cpp",
    "MT7902BluetoothFirmware/MT7902BtProtocol.cpp",
    "MT7902BluetoothFirmware/MT7902BluetoothFirmware.cpp",
]

BT_HEADERS = [
    "MT7902BluetoothFirmware/MT7902BtProtocol.hpp",
    "MT7902BluetoothFirmware/MT7902BluetoothFirmware.hpp",
]

PLISTS = [
    "AirportMT7902/Info-Ventura.plist",
    "AirportMT7902/Info-Sonoma.plist",
    "AirportMT7902/Info-Sequoia.plist",
    "AirportMT7902/Info-Tahoe.plist",
    "MT7902BluetoothFirmware/Info.plist",
]

TARGETS = [
    {
        "name": "AirportMT7902-Ventura",
        "product_name": "AirportMT7902",
        "bundle_id": "com.mtk.AirportMT7902",
        "subdir": "Ventura",
        "plist": "AirportMT7902/Info-Ventura.plist",
        "deployment": "13.0",
        "sources": WIFI_SOURCES,
        "defines": [
            "__PRIVATE_SPI__",
            "AIRPORT",
            "__IO80211_TARGET=__MAC_13_0",
        ],
    },
    {
        "name": "AirportMT7902-Sonoma",
        "product_name": "AirportMT7902",
        "bundle_id": "com.mtk.AirportMT7902",
        "subdir": "Sonoma",
        "plist": "AirportMT7902/Info-Sonoma.plist",
        "deployment": "14.4",
        "sources": WIFI_SOURCES,
        "defines": [
            "__PRIVATE_SPI__",
            "AIRPORT",
            "IO80211FAMILY_V2",
            "__IO80211_TARGET=__MAC_14_4",
        ],
    },
    {
        "name": "AirportMT7902-Sequoia",
        "product_name": "AirportMT7902",
        "bundle_id": "com.mtk.AirportMT7902",
        "subdir": "Sequoia",
        "plist": "AirportMT7902/Info-Sequoia.plist",
        "deployment": "15.2",
        "sources": WIFI_SOURCES,
        "defines": [
            "__PRIVATE_SPI__",
            "AIRPORT",
            "IO80211FAMILY_V2",
            "AIRPORT_WCL",
            "__IO80211_TARGET=__MAC_15_2",
        ],
    },
    {
        "name": "AirportMT7902-Tahoe",
        "product_name": "AirportMT7902",
        "bundle_id": "com.mtk.AirportMT7902",
        "subdir": "Tahoe",
        "plist": "AirportMT7902/Info-Tahoe.plist",
        "deployment": "15.2",
        "sources": WIFI_SOURCES,
        "defines": [
            "__PRIVATE_SPI__",
            "AIRPORT",
            "IO80211FAMILY_V2",
            "AIRPORT_WCL",
            "__IO80211_TARGET=__MAC_26_0",
        ],
    },
    {
        "name": "MT7902BluetoothFirmware",
        "product_name": "MT7902BluetoothFirmware",
        "bundle_id": "com.mtk.MT7902BluetoothFirmware",
        "subdir": "Bluetooth",
        "plist": "MT7902BluetoothFirmware/Info.plist",
        "deployment": "12.0",
        "sources": BT_SOURCES,
        "defines": [
            "__PRIVATE_SPI__",
        ],
    },
]


def quote_pbx(val: str) -> str:
    if all(c.isalnum() or c in "._/" for c in val) and val:
        return val
    escaped = val.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def generate_scheme(target_id: str, target_name: str, product_name: str) -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<Scheme
   LastUpgradeVersion = "1600"
   version = "1.7">
   <BuildAction
      parallelizeBuildables = "YES"
      buildImplicitDependencies = "YES">
      <BuildActionEntries>
         <BuildActionEntry
            buildForTesting = "YES"
            buildForRunning = "YES"
            buildForProfiling = "YES"
            buildForArchiving = "YES"
            buildForAnalyzing = "YES">
            <BuildableReference
               BuildableIdentifier = "primary"
               BlueprintIdentifier = "{target_id}"
               BuildableName = "{product_name}.kext"
               BlueprintName = "{target_name}"
               ReferencedContainer = "container:MT7902MacOS.xcodeproj">
            </BuildableReference>
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <LaunchAction
      buildConfiguration = "Debug"
      selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB"
      selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB"
      launchStyle = "0"
      useCustomWorkingDirectory = "NO"
      ignoresPersistentStateOnLaunch = "NO"
      debugDocumentVersioning = "YES"
      allowLocationSimulation = "YES">
   </LaunchAction>
   <ArchiveAction
      buildConfiguration = "Release"
      revealArchiveInOrganizer = "YES">
   </ArchiveAction>
</Scheme>
"""


def main() -> None:
    repo_root = Path(__file__).resolve().parent.parent
    proj_dir = repo_root / "MT7902MacOS.xcodeproj"
    schemes_dir = proj_dir / "xcshareddata" / "xcschemes"
    schemes_dir.mkdir(parents=True, exist_ok=True)

    all_files = sorted(set(WIFI_SOURCES + WIFI_HEADERS + BT_SOURCES + BT_HEADERS + PLISTS))
    file_refs: dict[str, str] = {}
    for f in all_files:
        file_refs[f] = pbx_id(f"fileref:{f}")

    libkmod_ref = pbx_id("fileref:MacKernelSDK/Library/x86_64/libkmod.a")

    pbx_build_files: list[str] = []
    pbx_file_refs: list[str] = []
    pbx_fw_phases: list[str] = []
    pbx_src_phases: list[str] = []
    pbx_targets: list[str] = []
    pbx_configs: list[str] = []
    pbx_config_lists: list[str] = []

    for f in all_files:
        fid = file_refs[f]
        name = Path(f).name
        if f.endswith(".cpp"):
            ftype = "sourcecode.cpp.cpp"
        elif f.endswith(".hpp") or f.endswith(".h") or f.endswith(".pch"):
            ftype = "sourcecode.cpp.h"
        elif f.endswith(".plist"):
            ftype = "text.plist.xml"
        else:
            ftype = "text"
        pbx_file_refs.append(
            f"\t\t{fid} /* {name} */ = {{isa = PBXFileReference; fileEncoding = 4; lastKnownFileType = {ftype}; name = {quote_pbx(name)}; path = {quote_pbx(f)}; sourceTree = \"<group>\"; }};"
        )

    pbx_file_refs.append(
        f"\t\t{libkmod_ref} /* libkmod.a */ = {{isa = PBXFileReference; lastKnownFileType = archive.ar; name = libkmod.a; path = MacKernelSDK/Library/x86_64/libkmod.a; sourceTree = \"<group>\"; }};"
    )

    product_refs: list[str] = []
    target_ids: list[str] = []

    for tgt in TARGETS:
        tname = tgt["name"]
        pname = tgt["product_name"]
        tid = pbx_id(f"target:{tname}")
        target_ids.append(tid)
        prod_ref = pbx_id(f"product:{tname}")
        product_refs.append(prod_ref)

        pbx_file_refs.append(
            f"\t\t{prod_ref} /* {pname}.kext */ = {{isa = PBXFileReference; explicitFileType = wrapper.cfbundle; includeInIndex = 0; path = {quote_pbx(pname + '.kext')}; sourceTree = BUILT_PRODUCTS_DIR; }};"
        )

        # Sources phase
        src_build_ids = []
        for s in tgt["sources"]:
            bfid = pbx_id(f"buildfile:{tname}:{s}")
            src_build_ids.append((bfid, Path(s).name))
            pbx_build_files.append(
                f"\t\t{bfid} /* {Path(s).name} in Sources */ = {{isa = PBXBuildFile; fileRef = {file_refs[s]} /* {Path(s).name} */; }};"
            )

        src_phase_id = pbx_id(f"srcphase:{tname}")
        src_files_block = "\n".join(f"\t\t\t\t{bfid} /* {nm} in Sources */," for bfid, nm in src_build_ids)
        pbx_src_phases.append(
            f"""\t\t{src_phase_id} /* Sources */ = {{
\t\t\tisa = PBXSourcesBuildPhase;
\t\t\tbuildActionMask = 2147483647;
\t\t\tfiles = (
{src_files_block}
\t\t\t);
\t\t\trunOnlyForDeploymentPostprocessing = 0;
\t\t}};"""
        )

        # Frameworks phase (libkmod.a)
        kmod_bfid = pbx_id(f"buildfile:{tname}:libkmod.a")
        pbx_build_files.append(
            f"\t\t{kmod_bfid} /* libkmod.a in Frameworks */ = {{isa = PBXBuildFile; fileRef = {libkmod_ref} /* libkmod.a */; }};"
        )
        fw_phase_id = pbx_id(f"fwphase:{tname}")
        pbx_fw_phases.append(
            f"""\t\t{fw_phase_id} /* Frameworks */ = {{
\t\t\tisa = PBXFrameworksBuildPhase;
\t\t\tbuildActionMask = 2147483647;
\t\t\tfiles = (
\t\t\t\t{kmod_bfid} /* libkmod.a in Frameworks */,
\t\t\t);
\t\t\trunOnlyForDeploymentPostprocessing = 0;
\t\t}};"""
        )

        # Configurations (Debug & Release)
        dbg_cfg_id = pbx_id(f"cfg:{tname}:Debug")
        rel_cfg_id = pbx_id(f"cfg:{tname}:Release")
        cfg_list_id = pbx_id(f"cfglist:{tname}")

        defs_lines = "\n".join(f"\t\t\t\t\t{quote_pbx(d)}," for d in tgt["defines"])
        for cfg_name, cfg_id in [("Debug", dbg_cfg_id), ("Release", rel_cfg_id)]:
            pbx_configs.append(
                f"""\t\t{cfg_id} /* {cfg_name} */ = {{
\t\t\tisa = XCBuildConfiguration;
\t\t\tbuildSettings = {{
\t\t\t\tCONFIGURATION_BUILD_DIR = "$(SYMROOT)/$(CONFIGURATION)/{tgt['subdir']}";
\t\t\t\tCURRENT_PROJECT_VERSION = "$(MODULE_VERSION)";
\t\t\t\tFRAMEWORK_SEARCH_PATHS = "";
\t\t\t\tGCC_PRECOMPILE_PREFIX_HEADER = YES;
\t\t\t\tGCC_PREFIX_HEADER = AirportMT7902/PrivateSPI.pch;
\t\t\t\tGCC_PREPROCESSOR_DEFINITIONS = (
\t\t\t\t\t"$(inherited)",
{defs_lines}
\t\t\t\t);
\t\t\t\tINFOPLIST_FILE = {quote_pbx(tgt['plist'])};
\t\t\t\tLIBRARY_SEARCH_PATHS = (
\t\t\t\t\t"$(inherited)",
\t\t\t\t\t"$(PROJECT_DIR)/MacKernelSDK/Library/x86_64",
\t\t\t\t);
\t\t\t\tMACOSX_DEPLOYMENT_TARGET = {tgt['deployment']};
\t\t\t\tMODULE_NAME = {tgt['bundle_id']};
\t\t\t\tPRODUCT_BUNDLE_IDENTIFIER = {tgt['bundle_id']};
\t\t\t\tPRODUCT_MODULE_NAME = {pname};
\t\t\t\tPRODUCT_NAME = {pname};
\t\t\t\tSYSTEM_HEADER_SEARCH_PATHS = "include";
\t\t\t\tWRAPPER_EXTENSION = kext;
\t\t\t}};
\t\t\tname = {cfg_name};
\t\t}};"""
            )

        pbx_config_lists.append(
            f"""\t\t{cfg_list_id} /* Build configuration list for PBXNativeTarget "{tname}" */ = {{
\t\t\tisa = XCConfigurationList;
\t\t\tbuildConfigurations = (
\t\t\t\t{dbg_cfg_id} /* Debug */,
\t\t\t\t{rel_cfg_id} /* Release */,
\t\t\t);
\t\t\tdefaultConfigurationIsVisible = 0;
\t\t\tdefaultConfigurationName = Release;
\t\t}};"""
        )

        pbx_targets.append(
            f"""\t\t{tid} /* {tname} */ = {{
\t\t\tisa = PBXNativeTarget;
\t\t\tbuildConfigurationList = {cfg_list_id} /* Build configuration list for PBXNativeTarget "{tname}" */;
\t\t\tbuildPhases = (
\t\t\t\t{src_phase_id} /* Sources */,
\t\t\t\t{fw_phase_id} /* Frameworks */,
\t\t\t);
\t\t\tbuildRules = (
\t\t\t);
\t\t\tdependencies = (
\t\t\t);
\t\t\tname = {quote_pbx(tname)};
\t\t\tproductName = {pname};
\t\t\tproductReference = {prod_ref} /* {pname}.kext */;
\t\t\tproductType = "com.apple.product-type.kernel-extension";
\t\t}};"""
        )

        scheme_path = schemes_dir / f"{tname}.xcscheme"
        scheme_path.write_text(generate_scheme(tid, tname, pname), encoding="utf-8")

    # Project-level Debug & Release configurations
    proj_dbg_id = pbx_id("projcfg:Debug")
    proj_rel_id = pbx_id("projcfg:Release")
    proj_cfg_list_id = pbx_id("projcfglist")

    for cfg_name, cfg_id, opt_lvl, extra_def in [
        ("Debug", proj_dbg_id, "0", '\t\t\t\t\t"DEBUG=1",\n'),
        ("Release", proj_rel_id, "2", ""),
    ]:
        pbx_configs.append(
            f"""\t\t{cfg_id} /* {cfg_name} */ = {{
\t\t\tisa = XCBuildConfiguration;
\t\t\tbuildSettings = {{
\t\t\t\tALWAYS_SEARCH_USER_PATHS = NO;
\t\t\t\tARCHS = x86_64;
\t\t\t\tCLANG_CXX_LANGUAGE_STANDARD = "gnu++17";
\t\t\t\tCLANG_CXX_LIBRARY = "libc++";
\t\t\t\tCODE_SIGN_IDENTITY = "";
\t\t\t\tCODE_SIGN_STYLE = Manual;
\t\t\t\tCOPY_PHASE_STRIP = NO;
\t\t\t\tCURRENT_PROJECT_VERSION = "$(MODULE_VERSION)";
\t\t\t\tDEBUG_INFORMATION_FORMAT = dwarf;
\t\t\t\tGCC_C_LANGUAGE_STANDARD = c11;
\t\t\t\tGCC_NO_COMMON_BLOCKS = YES;
\t\t\t\tGCC_OPTIMIZATION_LEVEL = {opt_lvl};
\t\t\t\tGCC_PREPROCESSOR_DEFINITIONS = (
{extra_def}\t\t\t\t\tKERNEL,
\t\t\t\t\t"MT7902_DRIVER_VERSION=\\"${{MODULE_VERSION}}\\"",
\t\t\t\t);
\t\t\t\tKERNEL_EXTENSION_HEADER_SEARCH_PATHS = "$(PROJECT_DIR)/MacKernelSDK/Headers";
\t\t\t\tKERNEL_FRAMEWORK_HEADERS = "$(PROJECT_DIR)/MacKernelSDK/Headers";
\t\t\t\tMACOSX_DEPLOYMENT_TARGET = 12.0;
\t\t\t\tMODULE_VERSION = {MODULE_VERSION};
\t\t\t\tONLY_ACTIVE_ARCH = NO;
\t\t\t\tOTHER_CPLUSPLUSFLAGS = (
\t\t\t\t\t"-Wno-register",
\t\t\t\t\t"-Wno-deprecated-register",
\t\t\t\t\t"$(OTHER_CFLAGS)",
\t\t\t\t);
\t\t\t\tSDKROOT = macosx;
\t\t\t}};
\t\t\tname = {cfg_name};
\t\t}};"""
        )

    pbx_config_lists.append(
        f"""\t\t{proj_cfg_list_id} /* Build configuration list for PBXProject "MT7902MacOS" */ = {{
\t\t\tisa = XCConfigurationList;
\t\t\tbuildConfigurations = (
\t\t\t\t{proj_dbg_id} /* Debug */,
\t\t\t\t{proj_rel_id} /* Release */,
\t\t\t);
\t\t\tdefaultConfigurationIsVisible = 0;
\t\t\tdefaultConfigurationName = Release;
\t\t}};"""
    )

    main_group_id = pbx_id("group:main")
    products_group_id = pbx_id("group:products")
    project_obj_id = pbx_id("project:MT7902MacOS")

    main_children = "\n".join(
        f"\t\t\t\t{file_refs[f]} /* {Path(f).name} */," for f in all_files
    )
    prod_children = "\n".join(
        f"\t\t\t\t{pref} /* {t['product_name']}.kext */,"
        for pref, t in zip(product_refs, TARGETS)
    )
    target_list_block = "\n".join(
        f"\t\t\t\t{tid} /* {t['name']} */,"
        for tid, t in zip(target_ids, TARGETS)
    )

    pbxproj = f"""// !$*UTF8*$!
{{
\tarchiveVersion = 1;
\tclasses = {{
\t}};
\tobjectVersion = 54;
\tobjects = {{

/* Begin PBXBuildFile section */
{chr(10).join(pbx_build_files)}
/* End PBXBuildFile section */

/* Begin PBXFileReference section */
{chr(10).join(pbx_file_refs)}
/* End PBXFileReference section */

/* Begin PBXFrameworksBuildPhase section */
{chr(10).join(pbx_fw_phases)}
/* End PBXFrameworksBuildPhase section */

/* Begin PBXGroup section */
\t\t{main_group_id} = {{
\t\t\tisa = PBXGroup;
\t\t\tchildren = (
{main_children}
\t\t\t\t{libkmod_ref} /* libkmod.a */,
\t\t\t\t{products_group_id} /* Products */,
\t\t\t);
\t\t\tsourceTree = "<group>";
\t\t}};
\t\t{products_group_id} /* Products */ = {{
\t\t\tisa = PBXGroup;
\t\t\tchildren = (
{prod_children}
\t\t\t);
\t\t\tname = Products;
\t\t\tsourceTree = "<group>";
\t\t}};
/* End PBXGroup section */

/* Begin PBXNativeTarget section */
{chr(10).join(pbx_targets)}
/* End PBXNativeTarget section */

/* Begin PBXProject section */
\t\t{project_obj_id} /* Project object */ = {{
\t\t\tisa = PBXProject;
\t\t\tattributes = {{
\t\t\t\tLastUpgradeCheck = 1600;
\t\t\t}};
\t\t\tbuildConfigurationList = {proj_cfg_list_id} /* Build configuration list for PBXProject "MT7902MacOS" */;
\t\t\tcompatibilityVersion = "Xcode 14.0";
\t\t\tdevelopmentRegion = en;
\t\t\thasScannedForEncodings = 0;
\t\t\tknownRegions = (
\t\t\t\ten,
\t\t\t);
\t\t\tmainGroup = {main_group_id};
\t\t\tproductRefGroup = {products_group_id} /* Products */;
\t\t\tprojectDirPath = "";
\t\t\tprojectRoot = "";
\t\t\ttargets = (
{target_list_block}
\t\t\t);
\t\t}};
/* End PBXProject section */

/* Begin PBXSourcesBuildPhase section */
{chr(10).join(pbx_src_phases)}
/* End PBXSourcesBuildPhase section */

/* Begin XCBuildConfiguration section */
{chr(10).join(pbx_configs)}
/* End XCBuildConfiguration section */

/* Begin XCConfigurationList section */
{chr(10).join(pbx_config_lists)}
/* End XCConfigurationList section */
\t}};
\trootObject = {project_obj_id} /* Project object */;
}}
"""

    pbxproj_path = proj_dir / "project.pbxproj"
    pbxproj_path.write_text(pbxproj, encoding="utf-8")
    print(f"[OK] Generated {pbxproj_path} and {len(TARGETS)} shared schemes.")


if __name__ == "__main__":
    main()
