# MR-ADV

Cardputer ADV firmware and research for controlling the configured LG OLED C1. The current prototype uses Wi-Fi and webOS SSAP, so it does not require Bluetooth packet capture or native Magic Remote pairing.

## Current firmware

`src/main.cpp` connects to the saved Wi-Fi and TV address, registers an SSAP client on the TV's secure WebSocket, requests the separate network-input pointer socket, and maps Cardputer input to navigation, click, text, and gyro-driven pointer movement. SSAP key and Wi-Fi settings are stored in ESP32 NVS. The TV must be awake for initial pairing and the connection prompt must be accepted on screen.

The Wi-Fi pointer and navigation path has now been verified on the TV. A `MOVE 120 0` command produced a relative X event (`REL_X=120`) on the TV's `LGE Smart Remote - TouchPad` input node, and `BUTTON RIGHT` produced a Linux `KEY_RIGHT` press/release on its `Smart Remote RCU Input` node. The air pointer integrates gyro rate over elapsed time, applies low-pass smoothing and a 3 dps deadband, accumulates fractional pixels, and inverts both axes to correct the direction the user observed. The latest tuning targets 50 Hz updates, an 80 ms filter time constant, and 13.22 pixels per degree (about 10% above the original sensitivity). Enter/OK clicks the pointer when gyro control is enabled and sends the TV's ENTER key when pointer motion is paused. The dashboard now includes connection, volume, and pointer status with power, volume, mute, and pointer actions; see the [TUI design](docs/tui-design.md). Standby uses SSAP and wake uses LAN magic packets, which depends on the TV's network wake setting. This Wi-Fi mode is a useful network remote; it does not emulate the Magic Remote's BLE identity or voice link.

The ESP32-S3 Wi-Fi radio is 2.4 GHz only. USB CDC on boot is enabled so the serial monitor can send setup commands; it also reports the configured SSID and Wi-Fi state without printing the password. Wi-Fi setup uses the USB serial monitor:

```text
WIFI <ssid>|<password>
TV <tv-ip>
```

Serial monitor speed is 115200. Set Wi-Fi first; the firmware connects to the default TV address from `device-map.md` unless you send a `TV` command. `STATUS` prints the saved SSID and link status, `PROBE` checks the TV's TCP ports, `MOVE <dx> <dy>` and `BUTTON <UP|DOWN|LEFT|RIGHT|BACK|HOME|ENTER>` send manual pointer/navigation actions, `CLICK` sends a pointer click, `POINTER ON|OFF` pauses/resumes gyro movement, and `POWER`, `VOLUP`, `VOLDOWN`, and `MUTE` exercise the corresponding remote actions. `TVMAC <mac>` updates the MAC used for Wake-on-LAN. `RESETKEY` clears the stored SSAP key so the TV can issue a new pairing with updated permissions. Keys: hold Fn and press `;` for Up, `,` for Left, `.` for Down, or `/` for Right; Enter clicks/OK, `0` toggles standby/wake, `-` / `=` change volume, `m` toggles mute, Backspace or `b` sends Back, `h` sends Home, `p` toggles air-pointer movement, and Tab opens help. WASD are no longer navigation shortcuts.

The Cardputer is paired to the TV and associates with the 2.4 GHz network. A permissions-array construction bug caused the earlier `401 insufficient permissions` response; that is fixed in the current build. The TV has accepted registration, returned a secure pointer socket, and the pointer socket connects. See [protocol research](research/magic-remote-protocol.md) for the input-event verification details.

## Build and flash

Install PlatformIO, then from this directory:

```sh
pio run -e m5cardputer-adv
pio run -e m5cardputer-adv -t upload
```

Use the ADV board's USB connection for upload and serial setup. The firmware uses the M5Stack Cardputer library, ArduinoJson, and Arduino WebSockets.

## Project files

- [Device map](device-map.md): read-only TV inventory.
- [Protocol research](research/magic-remote-protocol.md): SSAP and pointer socket research, including what remains uncertain.
- [TUI design](docs/tui-design.md): compact dashboard layout, key map, power behavior, and the next UI passes.
- `src/main.cpp`: working firmware prototype.

The longer term native BLE/M-RCU clone remains a separate, optional research track. A useful Wi-Fi remote does not prove BLE emulation.
# mr-adv
