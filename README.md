# CAIRN Dual + HamView — SenseCAP Indicator

Two complete firmwares in internal flash: **Meshtastic** and **MeshCore**, each with a CAIRN-inspired touchscreen and HamView. **Modes** changes protocol with a reboot; routine switching does not back up or restore SD images.

**[Open the web flasher](https://rf-yvy.github.io/CAIRN_dual_hamview/)** · **[Download the release](https://github.com/RF-YVY/CAIRN_dual_hamview/releases/tag/sensecap-v0.1.4-preview)**

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
python -m esptool --chip esp32s3 --port YOUR_PORT --baud 460800 write-flash 0x0 sensecap-dual-0.1.4-preview.factory.bin
```

The full 8 MB factory image contains both applications, fresh filesystems and erased user settings storage. Checking or unchecking a browser's optional erase prompt does not turn this image into a preserving update. Generic OTA replacement is disabled because the other application slot holds the other protocol.

## Standard setup and node names

Select the mode you want to configure before connecting its client. Meshtastic and MeshCore keep independent names, identities and radio settings. Normal phone-app setup is supported; installing repeater firmware is not required for observer uploads.

| Mode | On-device naming | Standard client setup |
| --- | --- | --- |
| Meshtastic | Gear/settings, then **User name** for short and long names; official client recommended for complete User settings | Meshtastic Android: **Settings > User**. Apple: **Settings > Device Configuration > User**. Web/USB and the Python CLI also support User configuration. |
| MeshCore | **Settings > Node / system** (called **System** in earlier releases), or **Tools > System / node name** | Connect the MeshCore companion app over BLE, or a compatible web/desktop companion client over USB. Set its friendly name, radio parameters, channels and optional advertised location there. |

Meshtastic CLI naming, while Meshtastic is active:

```sh
meshtastic --port YOUR_PORT --set-owner "My node" --set-owner-short "NODE"
```

The current inherited Meshtastic on-device name editor can clear the IsLicensed flag when it submits a name-only User update. Use the official app/CLI for naming when preserving licensed/other User flags; the full standard client API remains available.

Use the official region/modem and channel settings appropriate to your network. This image still has the hardware/frequency limits listed above. MeshCore has one friendly node name; Meshtastic has separate short and long names. After a MeshCore rename, restart to refresh its BLE advertising name and observer label. The MeshCore firmware remains a Companion: observer uploading does not make it a repeater or room server. Its USB connection speaks the companion protocol rather than a bare serial repeater console.

Setup references: [MeshCore companion setup](https://nodakmesh.org/meshcore/setup), [Meshtastic initial configuration](https://meshtastic.org/docs/getting-started/initial-config/), [Meshtastic User configuration](https://meshtastic.org/docs/configuration/radio/user/), and [Meshtastic configuration](https://meshtastic.org/docs/configuration/).

### Observer map location

An observer feed alone does not create a map pin. CoreScope's live map needs a node advertisement with a valid location. Configure the node's intended public coordinates in MeshCore, enable including that location in advertisements, then send an advertisement from the companion app after the observer connects. Do not use a precise location if you do not want it public.

Starting with v0.1.4, the observer uploads its own successfully transmitted advertisements as `direction: tx`, without fabricated RSSI/SNR. Other outgoing messages are excluded. The signed advertisement controls the name, role and location; disabling advertised location remains respected. No extra radio broadcasts are generated. Another observer can also supply the node's advertisement. Reload the analyzer after its first location-bearing advertisement and check the map's region/role filters if the pin is hidden. The main Map can show a companion marker with an observer star and OBSERVER badge. Observer registration and the on-air Companion role are independent; the Live view can display the Companion role without that extra badge.

## Included behavior

- Nine dashboard cards; swipe upward to reveal HamView, Files and Tools. Live RSSI graphs in both profiles.
- Separate protocol identities and internal filesystems, with local/UTC clock preferences shared between profiles.
- HamView DX alerts in both profiles, using the recipient's credentials.
- MeshCore chats, channels, contacts, discovery, repeater login, quick-reply drafts and retained message history.
- MeshCore Bluetooth companion access with a random first-boot pairing PIN; optional trusted-LAN TCP companion on port 5000, disabled by default.
- Optional MeshCore observer uploads to a configurable MQTT broker, with editable topics, optional login/TLS, connection status and upload/queue/drop counters.
- MeshCore maps with visible-tile SD caching, receive-only spectrum/waterfall, SD text editing and JSON Lines history export (up to 128 retained records).

Maps need Wi-Fi for uncached tiles; there is no bulk offline-area download. Spectrum sweeps pause mesh reception/sending until stopped. The optional Wi-Fi companion permits full node control, so enable it only on a trusted network.

Observer setup: open **Tools → Observer / analyzer** and enter the broker hostname (without a URL scheme or path), port, optional MQTT username/password, and TLS setting. Fresh installations start with uploads off, a blank broker and region, and no credentials. Use the host, port and authentication supplied by your analyzer or private broker. TLS verifies the server against the bundled public CA roots and requires a synchronized clock.

The editable publish-topic template defaults to `meshcore/{region}/{pubkey}/{type}`. `{pubkey}` expands to the node's public key; `{type}` is required and expands to `packets` or `status`. `{region}` is optional; fill the region field when your template uses it. Regions can contain up to 16 letters, digits, underscores or hyphens. Paths and region codes are case-sensitive. For example, `my-network/site-1/{pubkey}/{type}` requires no region field. Wildcards, unknown placeholders, and oversized topics are rejected. Disabling remains possible with incomplete connection settings.

The broker must accept LetsMesh-compatible observer packet/status JSON. Supported transports are MQTT over TCP and certificate-verified MQTT over TLS. WebSocket URLs, automatic signed-JWT generation/renewal, and custom/self-signed CA installation are not provided. As one optional example, Mississippi Mesh uses `mqtt.msmesh.org`, port `1883`, no login, and its own region codes; this is not prefilled for new users. Existing v0.1.2 observer settings migrate when using a matching app-only update.

Received on-air packets, RSSI/SNR, node name and public identity go to the selected broker. Its own transmitted advertisements are also uploaded; transmitted packets do not claim reception measurements. Private keys and Wi-Fi credentials are excluded; MQTT credentials are used only for the broker connection and are excluded from published packet/status JSON. Messaging continues while uploads run in a separate task. The queue holds 16 packets, with drops counted; MQTT QoS 0 counts successful writes rather than analyzer acknowledgements. Uploads stop when MeshCore is inactive. No MQTT traffic is retransmitted over LoRa.

## Privacy and validation

No saved owner names, node identities, private channel keys, contacts, messages, Wi-Fi credentials, HamAlert credentials, coordinates or Bluetooth bonds are distributed. The Meshtastic filesystem is empty; the MeshCore filesystem contains only `/meshcore.profile`. Both protocols create their identity/key material locally. One compiler diagnostic path was normalized without changing segment layout; the image checksum and SHA256 were regenerated and verified.

Both applications were built and exercised on a SenseCAP during development. Resident switching was measured at about 1.7 seconds into MeshCore and 4.8 seconds back to Meshtastic. The public package has integrity, clean-filesystem, privacy and browser checks. **The clean factory-install path and its new radio-setup gate have not been tested on a second blank device**, so this is a preview release. No new power-failure or full feature regression suite was run for this package. Custom broker login/TLS configuration and own-transmitted-advertisement reporting have build and compile-time validation checks, but have not been hardware-tested on the connected operator unit. Report problems through Issues; do not upload private full-flash backups.

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
