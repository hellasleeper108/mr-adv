# MR-ADV

Cardputer ADV firmware for controlling an LG OLED C1 over the local network. The current build uses webOS SSAP and its separate pointer-input WebSocket; it does not emulate the Magic Remote's Bluetooth identity or voice link.

## Current status

The Cardputer has paired with the C1, joined the 2.4 GHz network, and reconnected successfully after firmware updates. Pointer movement and directional input were verified at the TV's Linux input layer. The air pointer is tuned for a 50 Hz update target, an 80 ms smoothing time constant, a 3 dps deadband, and 13.22 pixels per degree. The user confirmed the pointer direction and that the new landscape display orientation works well.

The landscape dashboard shows TV/Wi-Fi and SSAP status, volume and pointer state, a D-pad illustration, and power, volume, mute, and pointer controls. `Tab` opens a shortcut guide. See [TUI design](docs/tui-design.md) for the layout and planned UI work.

Standby, Wake-on-LAN, volume, and mute actions are implemented but still need direct on-TV verification. The power action sends `ssap://system/turnOff` while connected; when disconnected, it sends three Wake-on-LAN packets to the TV's Ethernet MAC. Wake requires the TV's network-wake feature and the local network to pass the broadcast packet. An offline TV may instead be unreachable for another reason, so the dashboard labels that state `WAKE` rather than asserting it is asleep.

## Cardputer controls

| Key | Action |
|---|---|
| Fn + `;` / `,` / `.` / `/` | Up / Left / Down / Right |
| Enter | Pointer click while air-pointer is on; navigation Enter while paused |
| `P` | Pause/resume gyro pointer movement |
| `H` / `B` | Home / Back |
| `0` | Standby if connected; otherwise send Wake-on-LAN |
| `-` / `=` | Volume down / up |
| `M` | Toggle mute |
| Tab | Toggle dashboard/help |

Action shortcuts take precedence over text insertion for their reserved keys. WASD are not navigation shortcuts.

## Serial setup and diagnostics

The USB serial monitor uses 115200 baud. SSID, password, TV address, TV MAC, and SSAP client key are stored in ESP32 NVS; the password and client key are not printed.

```text
WIFI <ssid>|<password>
TV <tv-ip>
TVMAC <xx:xx:xx:xx:xx:xx>
```

The default TV IP and Ethernet MAC come from [device-map.md](device-map.md). Useful serial commands:

- `STATUS` — show Wi-Fi, SSAP, pointer socket, and air-pointer state.
- `PROBE` — check TCP reachability of TV ports 3000 and 3001.
- `MOVE <dx> <dy>`, `BUTTON <name>`, and `CLICK` — send manual pointer input.
- `POINTER ON|OFF` — pause or resume gyro motion.
- `POWER`, `VOLUP`, `VOLDOWN`, and `MUTE` — trigger the matching remote action.
- `RESETKEY` — clear the saved SSAP client key and reconnect for registration.

The ESP32-S3 Wi-Fi radio uses 2.4 GHz. Initial pairing requires the TV to be awake and the registration request to be accepted on screen.

## Build and flash

Install PlatformIO, then run from this directory:

```sh
pio run -e m5cardputer-adv
pio run -e m5cardputer-adv -t upload
```

## Project files

- [Device map](device-map.md) — read-only TV inventory.
- [Protocol research](research/magic-remote-protocol.md) — SSAP/pointer findings and the separate native BLE research track.
- [TUI design](docs/tui-design.md) — dashboard layout, controls, power behavior, and next UI passes.
- `src/main.cpp` — Cardputer firmware.
- `platformio.ini` — PlatformIO environment and dependencies.
