# CAIRN Dual + HamView — SenseCAP Indicator

Two complete firmwares in internal flash: **Meshtastic** and **MeshCore**, each with a CAIRN-inspired touchscreen and HamView. **Modes** changes protocol with a reboot; routine switching does not back up or restore SD images.

**[Open the web flasher](https://rf-yvy.github.io/CAIRN_dual_hamview/)** · **[Download the release](https://github.com/RF-YVY/CAIRN_dual_hamview/releases/tag/sensecap-v0.1.0-preview)**

This is an independent implementation, not the original CAIRN firmware. Full CAIRN feature parity is not claimed. The inherited `docs/` tree and [upstream README](reference/CAIRN-upstream-README.md) describe the upstream CAIRN project; they are not this build's support matrix.

## Supported hardware

- SenseCAP Indicator **for Meshtastic**, ESP32-S3, **8 MB flash / 8 MB PSRAM**, SX1262 LoRa, 480×480 touch display.
- This preview's MeshCore radio configuration supports **902–928 MHz**. Other frequency variants and ordinary ESP32-S3 development boards are not supported by this image.
- RP2040 companion firmware **v2.1 / interdevice protocol v2** is recommended and is required for the documented SD functionality. The ESP32 web installer does not update the RP2040.

## Installation

**Fresh installation replaces the ESP32's settings, identities, keys, contacts and message history. It is not a settings-preserving update. The SD card is not formatted.** Back up anything you need before installation.

1. Open the [web flasher](https://rf-yvy.github.io/CAIRN_dual_hamview/) in desktop Chrome or Edge using a USB data cable.
2. Confirm the supported hardware and fresh-install notice. Connect the **ESP32 CH340 serial port**, not the RP2040 port. Select Install and leave power connected until completion.
3. If the SD bridge needs updating, use the separate official `indicator-rp2040-v2.1.uf2` from this release and follow the [RP2040 installation instructions](https://github.com/meshtastic/indicator_rp2040#installation). Put the RP2040 into BOOTSEL mode and copy the UF2 onto its drive. Never feed a UF2 file to the ESP32 installer.
4. Meshtastic starts first. Choose your own region, node name and channels.
5. Tap **Modes** to enter MeshCore. Its identity is generated on the device. Transmission is initially disabled: save your own Radio settings and scope to enable it. `us` and `us-ms` are optional scope shortcuts, not a preconfigured owner network.
6. Configure your own Wi-Fi and HamAlert login in HamView. Tap the large clock for UTC/local time and time-zone options. A cold start needs Wi-Fi/NTP or companion time before an accurate clock is available.

### Command line

Download the factory image from Releases, close other serial applications, and run:

```sh
python -m pip install esptool
python -m esptool --chip esp32s3 --port YOUR_PORT --baud 460800 write-flash 0x0 sensecap-dual-0.1.0-preview.factory.bin
```

The full 8 MB factory image contains both applications, fresh filesystems and erased user settings storage. Checking or unchecking a browser's optional erase prompt does not turn this image into a preserving update. Generic OTA replacement is disabled because the other application slot holds the other protocol.

## Included behavior

- Nine dashboard cards; swipe upward to reveal HamView, Files and Tools. Live RSSI graphs in both profiles.
- Separate protocol identities and internal filesystems, with local/UTC clock preferences shared between profiles.
- HamView DX alerts in both profiles, using the recipient's credentials.
- MeshCore chats, channels, contacts, discovery, repeater login, quick-reply drafts and retained message history.
- MeshCore Bluetooth companion access with a random first-boot pairing PIN; optional trusted-LAN TCP companion on port 5000, disabled by default.
- MeshCore maps with visible-tile SD caching, receive-only spectrum/waterfall, SD text editing and JSON Lines history export (up to 128 retained records).

Maps need Wi-Fi for uncached tiles; there is no bulk offline-area download. Spectrum sweeps pause mesh reception/sending until stopped. The optional Wi-Fi companion permits full node control, so enable it only on a trusted network.

## Privacy and validation

No saved owner names, node identities, private channel keys, contacts, messages, Wi-Fi credentials, HamAlert credentials, coordinates or Bluetooth bonds are distributed. The Meshtastic filesystem is empty; the MeshCore filesystem contains only `/meshcore.profile`. Both protocols create their identity/key material locally. One compiler diagnostic path was normalized without changing segment layout; the image checksum and SHA256 were regenerated and verified.

Both applications were built and exercised on a SenseCAP during development. Resident switching was measured at about 1.7 seconds into MeshCore and 4.8 seconds back to Meshtastic. The public package has integrity, clean-filesystem, privacy and browser checks. **The clean factory-install path and its new radio-setup gate have not been tested on a second blank device**, so this is a preview release. No new power-failure or full feature regression suite was run for this package. Report problems through Issues; do not upload private full-flash backups.

## Source and builds

Corresponding source is in [`firmware/`](firmware/): Meshtastic base `08cd97e`, the vendored device-ui modifications, and the MeshCore application with pinned dependencies. Your operating system must support the ESP32 PlatformIO toolchain. A short checkout path is recommended on Windows.

```sh
python -m pip install platformio
pio run -d firmware -e seeed-sensecap-indicator-cairn
pio run -d firmware/meshcore -e sensecap-meshcore-public
python tools/verify_sensecap_package.py
```

Use **`sensecap-meshcore-public`** for redistribution. The private/development environment retains its prior behavior and does not impose the first-boot radio confirmation gate. Never bake personal preferences, a device filesystem dump or NVS into a public build.

[`tools/package_sensecap.py`](tools/package_sensecap.py) constructs fresh filesystems and the merged image. It accepts `--bootloader`, `--partitions`, `--meshtastic`, `--meshcore`, `--mklittlefs`, `--mkspiffs`, `--rp2040`, and `--output` paths; use the PlatformIO-generated binaries/tools and the official RP2040 v2.1 UF2. Install `esptool` in the Python environment that runs it. Set output to `web/firmware`. The script verifies the layout, filesystem contents, checksums, blank settings region and merged parts. `web/manifest.json` and `SHA256SUMS.txt` are generated with the image. Rebuilding source in a new Git checkout can change build metadata and binary hashes.

### Resident flash layout

| Component | Offset | Allocated size |
| --- | --- | --- |
| NVS / user settings | `0x9000` | `0x5000` |
| OTA boot selection | `0xE000` | `0x2000` |
| Meshtastic | `0x10000` | `0x3E0000` |
| MeshCore | `0x3F0000` | `0x250000` |
| Meshtastic LittleFS | `0x640000` | `0x180000` |
| MeshCore SPIFFS | `0x7C0000` | `0x40000` |

Keep applications, partition table and filesystem images from a matching release. Legacy SD-loader app images and stock factory images are not interchangeable with this layout. Future larger applications may require a new layout; current space is limited.

## Licensing

Firmware changes and Meshtastic are GPL-3.0; MeshCore is MIT-licensed. Dependencies and inherited reference material retain their original notices. See [LICENSE](LICENSE), [SenseCAP notices](SENSECAP-NOTICES.txt), [third-party notices](THIRD_PARTY_NOTICES.md) and the license files accompanying the vendored source. The browser installer uses [ESP Web Tools](https://esphome.github.io/esp-web-tools/).
