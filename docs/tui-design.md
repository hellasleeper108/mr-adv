# MR-ADV remote UI

## Product goals

- Make TV connection and pointer mode obvious at a glance.
- Keep the core remote usable without looking at the Cardputer keyboard.
- Give every shortcut a discoverable on-device help screen.
- Keep navigation, pointer movement, and power behavior independent.
- Show uncertainty honestly: an unreachable TV may be asleep or simply offline.

## Screen layout

The Cardputer ADV display is set to rotation 0 for landscape operation at 240 × 135 pixels. The UI uses one compact dashboard and one shortcut page rather than a dense, multi-level menu.

```text
╭ MR-ADV       LG C1                  ● TV ON ╮
  WiFi OK  SSAP OK     VOL 18      AIR ON

          ┌─^─┐       ┌───────────────┐
    ┌─<─┐ ├ OK┤ ┌─>─┐ │ 0  POWER/WAKE │
    └───┘ └───┘ └───┘ ├──────┬────────┤
          ┌─v─┐       │ - VOL│ = VOL+ │
                      ├──────┼────────┤
                      │ MUTE │ P AIR  │
  Fn arrows   Enter OK   H Home   Tab ?
```

The live screen shows connection state, volume/mute state, air-pointer mode, a D-pad diagram, and the power/volume/mute/pointer actions. The `Tab` page lists the shortcuts. Press `Tab` again to return to the dashboard.

The user confirmed the landscape orientation and dashboard are comfortable to operate. Pointer movement and directional navigation have also been verified on the C1. Standby/wake, volume, and mute are implemented, but remain pending direct user verification; the TUI must not present a successful send as proof that the TV applied the action.

## Controls

| Key | Action |
|---|---|
| Fn + `;`, `,`, `.`, `/` | Up, Left, Down, Right |
| Enter | Pointer click while air-pointer is on; navigation Enter while paused |
| `P` | Pause/resume gyro pointer movement |
| `C` | Calibrate gyro bias; hold still through the brief calibration |
| `H` / `B` | Home / Back |
| `0` | Standby if connected; otherwise send Wake-on-LAN |
| `-` / `=` | Volume down / up |
| `M` | Toggle mute |
| Tab | Toggle dashboard/help |

These shortcuts are also sent to the TV's system text input when a text-entry context is active. Reserved action letters currently take precedence over text entry; a future text-entry mode should address that conflict.

## Power behavior

- While SSAP is registered, `0` sends `ssap://system/turnOff` and shows a standby status.
- When SSAP is unavailable, `0` sends three Wake-on-LAN magic packets to the TV's Ethernet MAC on the local broadcast network.
- The UI labels the disconnected state `WAKE`, since software cannot distinguish standby from a network outage. Wake depends on the TV's network wake setting and the router passing the local broadcast packet.
- Configure the TV's Ethernet MAC over serial with `TVMAC <xx:xx:xx:xx:xx:xx>` to enable Wake-on-LAN. No device-specific network address is included by default.

## Pointer tuning and diagnostics

- The IMU sampling loop targets 100 Hz and accumulates motion into 50 Hz pointer frames.
- A small stillness calibration gathers 80 gyro samples after a 250 ms settling interval. It rejects unstable readings, subtracts the measured X/Z bias, and saves an accepted bias in NVS for reuse after reboot.
- The low-pass filter uses a 100 ms time constant at rest and 45 ms during deliberate movement. A 3 dps per-axis deadband remains in place.
- Press `C` (or send `CALIBRATE` over serial) while holding the Cardputer still. `IMULOG ON` prints a 20 Hz trace with sample timing, raw gyro values, bias, filtered values, and pending pointer deltas; `IMULOG OFF` stops it.
- Keep the sample/send rates and sensitivity fixed while collecting stationary, slow-aim, and fast-sweep traces. Tune the deadband, filter, or gain curve from those readings one at a time.

Initial stationary bench trace (2026-11-17): calibration was accepted with approximately −0.09 dps X and Z bias. Observed sampling intervals were 10–11 ms, consistent with the 100 Hz target, and stationary raw gyro variation stayed far below the 3 dps deadband. This confirms the idle sensor is quiet; it does not yet characterize hand tremor or intentional slow aiming, so the deadband and gain have not been reduced.

## Next product passes

1. Add a source picker from `ssap://tv/getExternalInputList` and an app picker from `ssap://com.webos.applicationManager/listApps`.
2. Add media transport controls and channel shortcuts where the current source supports them.
3. Add a text-entry screen that temporarily disables action-letter shortcuts.
4. Add a settings page for pointer sensitivity, deadband, Wi-Fi/TV configuration, and wake behavior.
5. Surface battery level and clearer reconnect progress if the Cardputer library exposes stable readings.
6. Validate standby and wake on this C1, then verify volume and mute. WoL is a best-effort wake path, not a guaranteed toggle if the TV's LAN wake setting is disabled.
