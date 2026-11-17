# MR-ADV

Cardputer ADV firmware for controlling an LG OLED C1 over the local network. The current build uses webOS SSAP and its separate pointer-input WebSocket; it does not emulate the Magic Remote's Bluetooth identity or voice link.

## Current status

The Cardputer has paired with the C1, joined the 2.4 GHz network, and reconnected successfully after firmware updates. Pointer movement and directional input were verified at the TV's Linux input layer. The IMU is now sampled at a 100 Hz target while pointer deltas are sent at 50 Hz. Filtering adapts between a 100 ms idle time constant and a 45 ms active time constant, with a 3 dps deadband and 13.22 pixels per degree. Press `C` while holding the Cardputer still to calibrate gyro bias; an accepted calibration is saved in NVS and reused after reboot. The serial `IMULOG ON` command enables a 20 Hz trace for tuning. The user confirmed the pointer direction and that the landscape display orientation works well.

The landscape dashboard shows TV/Wi-Fi and SSAP status, volume and pointer state, a D-pad illustration, and power, volume, mute, and pointer controls. `Tab` opens a shortcut guide. See [TUI design](docs/tui-design.md) for the layout and planned UI work.

Standby, Wake-on-LAN, volume, and mute actions are implemented but still need direct on-TV verification. The power action sends `ssap://system/turnOff` while connected; when disconnected, it sends three Wake-on-LAN packets to the TV's Ethernet MAC. Wake requires the TV's network-wake feature and the local network to pass the broadcast packet. An offline TV may instead be unreachable for another reason, so the dashboard labels that state `WAKE` rather than asserting it is asleep.

## Cardputer controls

| Key | Action |
|---|---|
| Fn + `;` / `,` / `.` / `/` | Up / Left / Down / Right |
| Enter | Pointer click while air-pointer is on; navigation Enter while paused |
| `P` | Pause/resume gyro pointer movement |
| `C` | Calibrate gyro bias; hold the Cardputer still until complete |
| `H` / `B` | Home / Back |
| `0` | Standby if connected; otherwise send Wake-on-LAN |
| `-` / `=` | Volume down / up |
| `M` | Toggle mute |
| Tab | Toggle dashboard/help |

Action shortcuts take precedence over text insertion for their reserved keys. WASD are not navigation shortcuts.

## Connect the Cardputer to Wi-Fi

The ESP32-S3 only supports 2.4 GHz Wi-Fi. Connect it to your router's 2.4 GHz SSID; the TV and Cardputer must be reachable on the same local network.

1. Build and flash the firmware using the commands below, then leave the Cardputer connected to USB.
2. From this project directory, list serial devices and note the Cardputer port:

   ```sh
   pio device list
   ```

   Linux ports look like `/dev/ttyACM0`; macOS ports often look like `/dev/cu.usbmodem...`; Windows ports look like `COM3`.
3. Open the serial monitor at 115200 baud, replacing `<PORT>` with the port from the previous step:

   ```sh
   pio device monitor -p <PORT> -b 115200
   ```

4. Enter this command in the monitor and press Enter, replacing the placeholders with your 2.4 GHz network name and password:

   ```text
   WIFI <ssid>|<password>
   ```

   The `|` separates the SSID from the password. Neither value may contain `|`. The firmware saves both in device NVS and reconnects immediately. It reports the SSID and assigned IP, but does not print the password.
5. Send `STATUS` in the serial monitor. Confirm Wi-Fi is connected and an IP address is shown. Set the TV's address with `TV <tv-ip>` and its Ethernet MAC with `TVMAC <xx:xx:xx:xx:xx:xx>` for Wake-on-LAN.
6. For the first connection, turn on the TV and accept the pairing prompt. The saved SSAP key is reused on later boots.

## Serial setup and diagnostics

The USB serial monitor uses 115200 baud. SSID, password, TV address, TV MAC, gyro bias, and SSAP client key are stored in ESP32 NVS; the password and client key are not printed.

```text
WIFI <ssid>|<password>
TV <tv-ip>
TVMAC <xx:xx:xx:xx:xx:xx>
```

TV network settings are not pre-filled; configure them for your own network. Useful serial commands:

- `STATUS` — show Wi-Fi, SSAP, pointer socket, and air-pointer state.
- `PROBE` — check TCP reachability of TV ports 3000 and 3001.
- `MOVE <dx> <dy>`, `BUTTON <name>`, and `CLICK` — send manual pointer input.
- `POINTER ON|OFF` — pause or resume gyro motion.
- `CALIBRATE` — measure gyro bias; hold the Cardputer still until complete.
- `IMULOG ON|OFF` — enable/disable a 20 Hz raw and filtered gyro trace.
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
