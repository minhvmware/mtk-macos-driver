# MediaTek MT7902 Native macOS Driver (`AirportMT7902` + `MT7902BluetoothFirmware` + `MT7902BluetoothPatcher`)

Bộ driver **Native macOS Kernel Extension (`.kext`)** viết bằng C++17 cho card mạng không dây **MediaTek MT7902 (Filogic 310 — Wi-Fi 6E PCIe + Bluetooth 5.2 USB)**, tích hợp trực tiếp vào menu Wi-Fi & Bluetooth gốc của macOS (không cần app phụ kiểu HeliPort và không cần OCLP root patch trên Sequoia/Tahoe).

## 1. Phần cứng hỗ trợ & hiệu năng đã kiểm chứng thực tế

| Thành phần | Giao tiếp | ID thiết bị | Kext đảm nhiệm |
| :--- | :--- | :--- | :--- |
| **Wi-Fi 6 / 6E (2.4 / 5 GHz 80 MHz, 802.11ax 1x1)** | PCIe (`0000:02:00.0`) | `14c3:7902` (`IOPCIMatch = 0x790214c3`, AzureWave `1a3b:5520` & OEMs) | `AirportMT7902.kext` |
| **Bluetooth 5.2 (WMT 79xx + BLE Scan Fix)** | USB (`Bus 001`) | `13d3:3579` (IMC/AzureWave), `13d3:3575`, `13d3:3584`, `13d3:3596`, `0489:e0f2`, `0489:e0f5`, `0e8d:7902` | `MT7902BluetoothFirmware.kext` + `MT7902BluetoothPatcher.kext` + `BlueToolFixup.kext` |

- **Hiệu năng đã đo thực tế trên macOS 15.8.3 Sequoia (VFIO passthrough `14c3:7902` trên router 5 GHz 80 MHz Wi-Fi 6)**:
  - `networkQuality -s -v` (đa luồng): **Downlink `213.1 Mbps` · Uplink `275.0 Mbps`** (`0 Ierrs / 0 Oerrs` sau >1.3 GB truyền nhận).
  - Tải 1 luồng HTTPS 25 MB: **20.0 – 20.6 MB/s (~165 Mbps)**, ngang chuẩn Linux native `mt7921e` trên cùng phần cứng.
  - Hỗ trợ đầy đủ RX A-MPDU Reorder Buffer (A-MSDU, BAR, 12-bit wraparound, timeout), TX Originator Block Ack (`ADDBA`) + `MT_TXD7_HW_AMSDU`, NAPI-style IRQ masking, zero-copy batched DMA (1024-entry TX/RX ring), zero-drop TX backpressure và XNU `kInputOptionQueuePacket`/`flushInputQueue()`.

## 2. Kiến trúc đa phiên bản macOS Native

Dự án tạo ra **6 scheme Xcode** (build ra `.kext` riêng cho từng ABI kernel của Apple):

1. **`AirportMT7902-Ventura`** (`__MAC_13_0`): Kế thừa `IO80211Controller` + `IO80211Interface` truyền thống (Monterey 12 / Ventura 13).
2. **`AirportMT7902-Sonoma`** (`__MAC_14_4`, `IO80211FAMILY_V2`): Kế thừa `IO80211ControllerV2` + `IO80211SkywalkInterface` cho macOS 14 Sonoma.
3. **`AirportMT7902-Sequoia`** (`__MAC_15_2`, `IO80211FAMILY_V2` + `AIRPORT_WCL`): Hỗ trợ kiến trúc **Native WCL (`IO80211InfraProtocol` / `IO80211InfraInterface`, `BeaconMetadata` `0x40` byte)** trên macOS 15.2+ **không cần OCLP**.
4. **`AirportMT7902-Tahoe`** (`__MAC_26_0`, `IO80211FAMILY_V2` + `AIRPORT_WCL`): Hỗ trợ kiến trúc **Native WCL macOS 26 Tahoe (`BeaconMetadata` `0x44` byte)** **không cần OCLP**.
5. **`MT7902BluetoothFirmware`**: Nạp firmware `BT_RAM_CODE_MT7902_1_1_hdr.bin` qua giao thức USB Control WMT (`0xfc6f` / `0xe4`), reset endpoint `0x74011890`, bật `BTMTK_WMT_FUNC_CTRL` rồi nhường thiết bị cho stack Bluetooth gốc của macOS.
6. **`MT7902BluetoothPatcher`** (Lilu plugin): Tự động tiêm `HCI_LE_Set_Random_Address` trước lệnh LE Scan và vá kiểm tra VM cho `bluetoothd`/`BlueTool` để quét & ghép đôi thiết bị Bluetooth BLE ổn định trên macOS 12–26.

### Các quirk phần cứng riêng của chip MT7902 đã xử lý trong HAL
- **TX Queue MCU-WM dùng Ring `15`** (`MT7902_TXQ_MCU_WM = 15`, khác với MT7921 dùng Ring `17`).
- **Không có MCU-WA Ring** (`HOST_RX_DONE_INT_ENA4` tắt hoàn toàn).
- **RX Ring 0 kích thước `512` descriptor**, dùng chung cho MCU Event, TXS và `PKT_TYPE_TXRX_NOTIFY` (giải phóng TX token).
- **Bảng Prefetch WFDMA0 riêng của MT7902** (`RX0..3`, `TX0..6`, `TX15`, `TX16`).
- **Tắt Runtime Deep Sleep mặc định** theo đúng quirk ổn định của `mt7921/init.c` cho chip `0x7902`.
- **DMA 32-bit physical mask** (`0x00000000fffff000ULL`) thông qua `IOBufferMemoryDescriptor::inTaskWithPhysicalMask`.

## 3. Cấu trúc thư mục

- `AirportMT7902/HAL/` — Thanh ghi BAR0 + L1 remap, WFDMA0 ring, Connac2 MCU, trình nạp ROM Patch + WM RAM, MAC TXWI/HW-TXP & RXD/P-RXV.
- `AirportMT7902/Net80211/` — Bảng kênh 2.4/5/6 GHz, parser Beacon/RSN/HT/VHT/HE IE, Scan Cache & máy trạng thái 802.11 STA (Open / WPA2 / WPA3).
- `AirportMT7902/Native/` — Lớp bọc `IOPCIDevice`, `IO80211Controller` (V1), `IO80211ControllerV2` + Skywalk + WCL (Sonoma / Sequoia / Tahoe).
- `MT7902BluetoothFirmware/` — Giao thức USB WMT 79xx & kext `IOUSBHostDevice`.
- `scripts/pack_firmware.py` — Đóng gói & nén `zlib` 3 file firmware thật từ `/lib/firmware/mediatek/` vào bảng C++.
- `scripts/generate_xcode_project.py` — Sinh `MT7902MacOS.xcodeproj` và 5 Xcode scheme.
- `scripts/vfio-macos-vm/` — Bộ script kiểm tra IOMMU, bind/unbind `vfio-pci` và chạy máy ảo macOS KVM (`docker-osx`) passthrough card MT7902 thật.
- `tests/test_mt7902.cpp` + `Dockerfile` — Bộ test phần cứng giả lập C++17 chạy trực tiếp trên Linux hoặc trong container Podman/Docker.

## 4. Test ngay trên Linux bằng Container (Podman / Docker) — Phương án 3

Kiểm tra toàn bộ giải nén firmware thật, BAR0/L1 remap, `probeAndInit()` nạp >800 KB firmware Wi-Fi qua WFDMA0, TXWI/TX-free token, 802.11ax Scan + WPA2 handshake, WCL Sequoia/Tahoe và nạp >490 KB firmware Bluetooth USB:

```bash
# Chạy trực tiếp trên máy host
make test

# Hoặc chạy đóng gói hoàn toàn trong container Podman / Docker
make container-test
```

## 5. Test thực tế với máy ảo macOS KVM + VFIO / USB Passthrough — Phương án 2

1. **Kiểm tra trạng thái KVM, IOMMU và thiết bị trên máy Fedora của bạn:**
   ```bash
   make vfio-check
   ```
   > **Lưu ý:** Nếu `check_iommu.sh` báo IOMMU chưa bật trong kernel boot args, chạy lệnh mà script in ra (`sudo grubby --update-kernel=ALL --args="intel_iommu=on iommu=pt"` hoặc `amd_iommu=on iommu=pt`) rồi `sudo reboot`. Riêng **Bluetooth USB (`13d3:3579`)** có thể passthrough ngay cả khi chưa bật IOMMU (`MT7902_SKIP_VFIO=1`).

2. **Tách card MT7902 khỏi `mt7921e` sang `vfio-pci` và bật máy ảo macOS (`docker-osx`):**
   ```bash
   sudo ./scripts/vfio-macos-vm/bind_vfio_mt7902.sh
   sudo ./scripts/vfio-macos-vm/run_macos_kvm_passthrough.sh
   ```

3. **Trả lại Wi-Fi cho Linux sau khi tắt máy ảo macOS:**
   ```bash
   sudo ./scripts/vfio-macos-vm/unbind_vfio_mt7902.sh
   ```

## 6. Build `.kext` bằng Xcode hoặc GitHub Actions

- **Trên GitHub Actions:** Đẩy repo lên GitHub — workflow `.github/workflows/build.yml` tự động chạy toàn bộ unit test và build cả 5 bản `.kext` (`Ventura`, `Sonoma`, `Sequoia`, `Tahoe`, `MT7902BluetoothFirmware`) trên runner `macos-15-intel`.
- **Trên macOS có Xcode:**
  ```bash
  git clone https://github.com/acidanthera/MacKernelSDK.git
  python3 scripts/pack_firmware.py          # nếu cần đóng gói lại firmware
  python3 scripts/generate_xcode_project.py # tạo MT7902MacOS.xcodeproj
  xcodebuild -project MT7902MacOS.xcodeproj -scheme AirportMT7902-Sequoia -configuration Release CODE_SIGNING_ALLOWED=NO
  xcodebuild -project MT7902MacOS.xcodeproj -scheme MT7902BluetoothFirmware -configuration Release CODE_SIGNING_ALLOWED=NO
  ```

## 7. Cấu hình OpenCore (`config.plist`) khi cài `.kext` vào macOS

1. Copy đúng phiên bản `AirportMT7902.kext` (ứng với bản macOS đang chạy) và `MT7902BluetoothFirmware.kext` vào `EFI/OC/Kexts/`.
2. Với **Bluetooth trên macOS 12+**, thêm **`BlueToolFixup.kext`** (từ gói `BrcmPatchRAM` của Acidanthera) sau `MT7902BluetoothFirmware.kext`, và đảm bảo cổng USB gắn Bluetooth (`13d3:3579`) được map `UsbConnector = 255` (Internal) trong `UTBMap.kext` / `USBMap.kext`.
3. Trên **macOS 14 Sonoma**, đảm bảo `IOSkywalkFamily.kext` / `IO80211FamilyLegacy` không chặn kext bên thứ ba hoặc bật `amfi=0x80` / `-lilubetaall` khi test bản chưa ký số (`kext-dev-mode=1` / tắt SIP một phần `csr-active-config = 03080000`).

## Giấy phép

Phát hành theo giấy phép **GPL-2.0-only** (tương thích với nguồn tham chiếu `mt76/mt7921`, `btmtk` từ Linux kernel và `itlwm`).
