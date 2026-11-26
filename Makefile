# SPDX-License-Identifier: GPL-2.0-only
# Makefile for MediaTek MT7902 macOS Native Driver (AirportMT7902 + MT7902BluetoothFirmware)

CXX ?= clang++
PYTHON ?= python3
CONTAINER_ENGINE := $(shell command -v podman 2>/dev/null || command -v docker 2>/dev/null)

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude
LDFLAGS := -lz

TEST_BIN := build/test_mt7902
HOST_SRCS := \
	AirportMT7902/Firmware/MT7902WifiFwData.cpp \
	AirportMT7902/HAL/MT7902Dma.cpp \
	AirportMT7902/HAL/MT7902Mcu.cpp \
	AirportMT7902/HAL/MT7902Firmware.cpp \
	AirportMT7902/HAL/MT7902Mac.cpp \
	AirportMT7902/HAL/MT7902Device.cpp \
	AirportMT7902/Net80211/MT7902Channels.cpp \
	AirportMT7902/Net80211/MT7902BeaconParser.cpp \
	AirportMT7902/Net80211/MT7902Wpa2.cpp \
	AirportMT7902/Net80211/MT7902Sta.cpp \
	AirportMT7902/Native/MT7902IOKitBus.cpp \
	AirportMT7902/Native/MT7902PCIWrapper.cpp \
	AirportMT7902/Native/AirportMT7902Interface.cpp \
	AirportMT7902/Native/AirportMT7902ControllerV1.cpp \
	AirportMT7902/Native/AirportMT7902EthernetInterface.cpp \
	AirportMT7902/Native/AirportMT7902SkywalkInterface.cpp \
	AirportMT7902/Native/AirportMT7902ControllerV2.cpp \
	MT7902BluetoothFirmware/Firmware/MT7902BtFwData.cpp \
	MT7902BluetoothFirmware/MT7902BtProtocol.cpp \
	MT7902BluetoothFirmware/MT7902BluetoothFirmware.cpp \
	tests/test_mt7902.cpp

.PHONY: all fw xcode plist-check test container-test vfio-check clean

all: xcode test

fw:
	$(PYTHON) scripts/pack_firmware.py

xcode:
	$(PYTHON) scripts/generate_xcode_project.py

plist-check:
	$(PYTHON) -c 'import plistlib, glob; files = sorted(glob.glob("**/*.plist", recursive=True)); assert files; [plistlib.load(open(p, "rb")) for p in files]; print(f"[OK] Validated {len(files)} Info.plist files.")'

$(TEST_BIN): $(HOST_SRCS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(HOST_SRCS) -o $(TEST_BIN) $(LDFLAGS)

test: plist-check $(TEST_BIN)
	./$(TEST_BIN)

container-test:
	@if [ -z "$(CONTAINER_ENGINE)" ]; then \
		echo "Error: neither podman nor docker found in PATH"; exit 1; \
	fi
	$(CONTAINER_ENGINE) build -t mt7902-macos-test -f Dockerfile .
	$(CONTAINER_ENGINE) run --rm mt7902-macos-test

vfio-check:
	bash scripts/vfio-macos-vm/check_iommu.sh

clean:
	rm -rf build
