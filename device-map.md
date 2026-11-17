# LG C1 device map for Cardputer ADV remote work

Captured 2026-11-16 from the configured `ssh lgtv` host. The collection was read only. No pairing, remote commands, file changes, or credentials were attempted.

## Device identity

| Field | Observed value | Notes |
|---|---|---|
| Configured product | LG OLED55C1PUB | From the local SSH host comment (`lgtv`, `lgtv-c1`, `lgwebostv`). The TV's own model JSON did not expose the retail model string. |
| Manufacturer / brand | LG Electronics / LG | `/etc/palm/TVModelInfo.json` |
| webOS | 6.5.3 (`ID=starfish`) | `/etc/os-release` |
| Software build stamp | `20250913080742` | `/etc/version` |
| OTA / platform identity | `HE_DTV_W21O_AFABATAA`, machine `O20N`, region `GLOBAL`, `webOSTV 6.0` | `/etc/starfish/model_id.json`; platform identity says 6.0 while runtime release is 6.5.3. |
| CPU / kernel | ARM64, Linux `4.4.84-229.kcl4tv.6`, build date 2025-09-13 | `uname -a` |
| Root filesystem | SquashFS read only, with writable overlay/data partitions | `/` is `/dev/mmcblk0p27`; data mounts include `/mnt/lg/db8`, `/mnt/lg/appstore`, `/mnt/lg/cmn_data`, `/mnt/lg/user`, `/var`, and `/home`. |
| Ethernet | `eth0`, `[REDACTED-TV-MAC]`, `[REDACTED-TV-IP]/24`; gateway `192.168.8.1` | Live interface state during capture. |
| Wi-Fi / P2P | `wlan0` was up but had no carrier; `p2p0` was dormant | The TV was using Ethernet during capture. |

## Exposed remote input devices

Linux input devices visible under `/sys/class/input`:

| Input name | Likely role |
|---|---|
| `LGE M-RCU - Builtin [0]` | Built in Magic Remote control interface |
| `LGE M-RCU - Builtin [1]` | Additional M-RCU interface |
| `LGE M-RCU - Builtin [2]` | Additional M-RCU interface |
| `LGE RCU` | Remote-control input interface |
| `LGE Simple Premium` | Premium remote input interface |
| `IoT keypad` | Separate keypad-like input interface |
| `Bluetooth-audio-source` | Bluetooth audio input, not a remote interface |

The kernel exposes `/dev/input/event*`, mouse nodes, and `/dev/uhid`; `/dev/cec0` is present for HDMI-CEC. The CEC IRQ/kernel driver is active. The remote input names prove that webOS routes M-RCU input through Linux input devices; they do not show the over-air Bluetooth protocol, pairing handshake, or motion data format.

## Network and services

Observed listening TCP ports from `/proc/net/tcp` and `/proc/net/tcp6`:

| Port | What is supported by the capture |
|---:|---|
| 22 | SSH (the configured root SSH access works) |
| 23 | Telnet listener; Homebrew Channel `telnetd` process was visible |
| 3000 | Common LG webOS remote WebSocket / SSAP port; listener present |
| 3001 | Common TLS WebSocket / SSAP port; listener present |
| 7000 | Listener present; commonly associated with AirPlay on LG TVs |
| 53 | Local DNS listener |
| 36866 | Listener present; service not identified from this capture |
| 18181, 34186, 39999 | Listeners present; service not identified from this capture |
| 1073, 1141, 1165, 1239, 1390, 1538, 1698, 1753, 7250, 39216, 40655 | Listeners present; service not identified from this capture |

Ports were enumerated from the live kernel table, without sending application traffic to them. The webOS System Property Luna request returned no response in the SSH session, so the API did not add identity detail. Do not treat the unassigned ports above as protocol-confirmed.

Other observed services/processes included `webos-bluetooth-service`, `irdbmanager`, `ssdp-discovery-lgtv`, Avahi, AirPlay, Miracast, webOS Luna Service Bus, WebAppMgr, and Homebrew Channel. The live process listing showed Homebrew `dropbear` and `telnetd`; only their presence was observed.

## Firmware implications

1. **Start with SSAP over Wi-Fi.** The TV has listeners on both 3000 and 3001, and the Cardputer ADV has Wi-Fi. This is the most practical first transport for navigation, media, volume, and text entry. Pair once through the TV prompt and persist the returned client key on the Cardputer; avoid requesting broad permissions when a narrower manifest works.
2. **Treat pointer control as a separate capability.** A phone-style SSAP remote can send ordinary navigation actions, but a Magic Remote experience may need the pointer channel and motion-to-pointer mapping. Test mouse movement, click, wheel, drag, long press, and reconnect behavior separately on this webOS 6.5.3 TV.
3. **Use the ADV's IR emitter for basic line-of-sight commands and fallback.** M5Stack lists IR transmission as a Cardputer ADV feature. IR can cover common power/volume/input keys if we determine the correct LG codes, but will not reproduce the paired pointer or voice functions.
4. **Defer Bluetooth Magic Remote emulation until evidence supports it.** The TV's M-RCU input nodes are useful reverse-engineering clues, not proof that the remote uses a standard BLE HID profile. First record real remote behavior and enumerate its advertised services while pairing. Do not disturb the currently paired physical remote.
5. **Keep the firmware modular:** transport (`SSAP Wi-Fi`, optional IR), key/action mapping, pointer/motion processing, pairing/key storage, and UI should be independent. The Cardputer ADV's ESP32-S3 supports Wi-Fi and BLE 5.0, but direct M-RCU emulation may still require a proprietary Bluetooth profile.

## Suggested next experiments

- Establish an SSAP client using a new client identity, accept its prompt on the TV, then read the registration response and verify a basic navigation key. Keep the physical Magic Remote available as recovery.
- Capture the official Magic Remote model from its label, then passively inspect advertising and connection metadata during a normal pairing workflow. Do not save or publish pairing secrets.
- Compare pointer behavior against the real remote and test the TV's `3000` and `3001` endpoints with a minimal client before writing embedded code.
- Confirm the ADV's onboard IR pin/driver against the board package already used by this repository. Test a benign key (for example volume up) before mapping power or standby.

## Sources

- Local observation: SSH host configuration and read-only commands on `[REDACTED-TV-IP]`, 2026-11-16.
- M5Stack official [Cardputer ADV product page](https://shop.m5stack.com/products/m5stack-cardputer-adv-version-esp32-s3) (ESP32-S3, BLE, IR transmission).
- Example open implementation of LG webOS remote over SSAP/WebSocket: [pmqt/lgremote](https://github.com/pmqt/lgremote). This is community documentation/code, not a protocol guarantee for every webOS release.
