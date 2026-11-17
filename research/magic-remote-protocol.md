# LG Magic Remote protocol research

Research updated 2026-11-17 for the configured LG OLED55C1PUB, reported by the TV as webOS 6.5.3. The local device inventory is in [../device-map.md](../device-map.md).

## Findings at a glance

| Question | Finding | Confidence |
|---|---|---|
| What is the remote likely to be? | The C1 is a 2021 TV and the expected matching remote family is MR21GA / AN-MR21GA. Verify the label on the remote before locking the target. | High, model match still to verify physically |
| Is its radio BLE? | LG documents Bluetooth for the 2021 Magic Remote; an FCC test report specifically identifies MR21GA and a Bluetooth LE test. | High |
| Is there a usable report format? | A community Linux driver for MR20 identifies Bluetooth HID ID `000f:3412` and parses report ID `0xFD`: counter, six signed motion samples, button code, wheel delta. | Medium-high for MR20; unverified for MR21GA/C1 |
| Does the TV calculate the Magic pointer? | The Linux implementation converts incoming gyro samples into relative mouse movement on the host. LG's own MRCU API also exposes pointer coordinates and sensor values to webOS apps. This supports host-side pointer processing, but is not direct proof of where the production C1 firmware does every stage. | Medium-high |
| Can Cardputer ADV implement it? | The ADV has ESP32-S3, BLE-capable radio, BMI270, and IR emitter. ESP-IDF supports BLE HID device examples on ESP32-S3. | High |
| What remains unknown? | C1 pairing/registration behavior for a fake M-RCU, exact BLE GATT report map and security requirements, MR21GA report compatibility, button semantics/edge cases, and voice transport. | Requires capture/testing |

## Likely Magic Remote radio protocol

LG's MR21GA manual describes Bluetooth communication and says motion or wheel movement wakes the on-screen pointer; pressing a direction button switches back to conventional remote operation. It identifies IR and Bluetooth transmitters on the remote. The FCC record names MR21GA and its test documentation labels the radio procedure Bluetooth LE, in the 2.4 GHz band. That makes BLE the strong target for this C1 remote family.

The public `brainrom/lg-magic` Linux driver recognizes a Bluetooth HID device ID `000f:3412`. Its `raw_event` handler accepts report ID `0xFD` and parses this layout:

| Offset | Width | Field | Encoding in driver |
|---:|---:|---|---|
| 0 | 1 | Report ID (`0xFD`) | unsigned byte |
| 1–2 | 2 | Packet counter | unsigned little-endian |
| 3–4 | 2 | Constant/reserved | ignored by parser |
| 5–16 | 12 | Gyro X/Y/Z, accel X/Y/Z | six signed big-endian 16-bit values |
| 17–18 | 2 | Button code | unsigned big-endian |
| 19 | 1 | Wheel delta | signed 8-bit |

The parser in `lg_magic_main.c` requires `size == 20`. Button codes observed by that driver mostly use `0x8000 | IR-command`, e.g. Home `0x807C`, Back `0x8028`, OK/wheel `0x8044`, microphone `0x808B`, arrows and media buttons. The wheel button is both Enter and mouse-left when in pointer mode. The driver treats gyro and accelerometer samples as raw IMU and computes airmouse movement using bias correction, scaling, low-pass filtering, and sensitivity parameters.

**Critical source inconsistency:** the repository's English README says report `0xFD` is 30 bytes total (1 + 29) while listing fields only through offset 19; the actual current parser rejects all lengths except 20. The protocol notes in the Cardputer project `IP9LGRemote` repeat the 20-byte form, but they are secondary notes, not a verified capture of MR21GA on this C1. Treat 20 bytes as the driver's implemented parser format, not yet as the final over-the-air contract. We must collect descriptor and packets from the actual remote before implementing native emulation.

The driver documents roughly 50 Hz IMU sampling, but its parser source does not enforce a timing interval. Measure the real remote. Likewise, report types `0xF9` and `0x01` are mentioned as unobserved in that project's README; their purpose is unknown. Voice may use a distinct path and is not covered by the known button report.

## What is known about pairing

LG's current support instructions say to turn on the TV, wait briefly, point the Magic Remote at it, and press the wheel/OK button to register. For the 2021 family, holding Home + Settings for five seconds starts reset/re-pair instructions and unregisters an already paired remote. Do not use that reset shortcut during casual experimentation because it can displace the existing remote.

No public source found here provides a complete C1-side M-RCU pairing trace. We do not yet know whether the TV accepts any conforming BLE HID peripheral with the expected descriptor, requires a name/appearance/vendor identity, checks a special feature report, or carries additional LG-specific registration/authentication. Therefore reproducing report bytes alone may not be enough.

The TV also supports generic Bluetooth keyboard, mouse, and gamepad pairing on webOS 6 and later. That gives us a reliable route to test BLE HID input, but a generic mouse is not native M-RCU emulation: it supplies relative cursor deltas rather than the Magic Remote's raw sensor/buttons/wheel behavior and may not reproduce voice, wake, or remote-specific controls.

## Three implementation levels

### Level 1: useful remote with minimal risk (current target)

- IR emitter for power and basic IR key codes.
- SSAP over TLS WebSocket (port 3001) for button actions, text, volume, apps, and network pointer socket.
- Persist SSAP client key using the firmware's settings storage; never log it.

This can make the Cardputer a practical remote without emulating Magic Remote Bluetooth.

The first Cardputer firmware slice is in [`../src/main.cpp`](../src/main.cpp). It connects to SSAP over TLS on port 3001, requests `getPointerInputSocket`, then uses the separate pointer WebSocket URL returned by the TV. The URL must be honored as returned: implementations report that the pointer service can use `ws://` on port 3000, distinct from the TLS SSAP connection. Pointer messages are newline-delimited fields, not JSON; examples include `type:button` + `name:UP`, `type:move` + `dx`/`dy`/`down:0`, `type:click`, and `type:scroll` + `dx`/`dy`. See the [reverse-engineered protocol notes](https://github.com/VesselWave/wave-remote/blob/main/docs/PROTOCOL.md), which were tested on webOS 24 and still need confirmation on this C1.

**Verified on this C1 (2026-11-17):** the Cardputer joined `GL-BE9300-0a5`, registered successfully, saved its SSAP client key, and connected to the TV-returned secure (`wss`) pointer socket on port 3001. Sending `MOVE 120 0` yielded `EV_REL / REL_X / 120` followed by `SYN_REPORT` on `/dev/input/event11`, which the TV identified as `LGE Smart Remote - TouchPad`. Sending `BUTTON RIGHT` yielded `EV_KEY / KEY_RIGHT / 1`, then `EV_KEY / KEY_RIGHT / 0`, on `/dev/input/event9` (`Smart Remote RCU Input`). Thus network pointer movement and directional-key delivery reach webOS's input layer. This validates the network control path, not yet the visible cursor position, pointer click, gyro calibration, native BLE pairing, standby wake, or voice. The TV creates the touchpad node dynamically, so event numbers can change; identify devices by name in `/proc/bus/input/devices` before capturing.

The firmware is running on the Cardputer and has paired to this TV. Initial SSAP registration required on-screen approval. Wi-Fi setup uses USB serial (`WIFI <ssid>|<password>`); the Wi-Fi credentials and client key are persisted in NVS. The prototype does not pin the TV's self-signed TLS certificate. Gyro axes, direction, sensitivity, and visible pointer behavior need hands-on tuning. Follow [`../README.md`](../README.md) for setup and controls.

### Level 2: Bluetooth air mouse

- Pair the Cardputer as a generic BLE mouse (optionally composite keyboard/media controls) using the C1's Connect Bluetooth Controller menu.
- Convert BMI270 motion into relative `dx/dy` and use wheel/keyboard for click/navigation.
- This proves the ADV BLE peripheral and TV HID path, but not M-RCU pairing. LG lists Bluetooth mice/keyboards as supported for webOS 6+.

### Level 3: native M-RCU clone

- Advertise the actual remote's required BLE/HID identity and exact report map.
- Bond/pair with the TV using the M-RCU flow, send correctly paced counter/IMU/button/wheel reports, and tune sensor axes and calibration against the C1's pointer.
- Use IR for power-on/standby because the TV may turn off its Bluetooth host while asleep; test wake behavior instead of assuming BLE can wake it.
- Determine whether microphone/voice traffic can be represented. A button code alone does not implement voice capture/transport. The ADV does have a microphone, but that does not establish compatibility with LG's voice path.

## What the LG ThinQ app can tell us

LG's ThinQ app provides a phone-based virtual remote. LG says it can move the TV pointer, scroll, use arrows and OK, change input, volume, and channel, and enter text. For those behaviors, ThinQ is a useful reference client even without the physical remote.

| ThinQ can help us validate | ThinQ cannot tell us |
|---|---|
| Whether pointer movement and scrolling work on this C1 and in which TV screens/apps | The Magic Remote's BLE GATT Report Map or `0xFD` report bytes |
| Which button actions the TV exposes through the network remote path | How the physical remote bonds, advertises, or registers as M-RCU |
| Text entry, input switching, volume/channel and app-control behavior | Raw Magic Remote IMU calibration, sample timing, packet counter, and wheel/button payloads |
| Whether an IP-based pointer transport can be a dependable firmware mode | Whether voice capture is sent over the Magic Remote HID link or another LG service |

ThinQ is a phone-to-TV remote session, so its pointer events are not a transparent relay of the missing handset's Bluetooth traffic. Community protocol implementations use the webOS SSAP `getPointerInputSocket` endpoint for cursor movement and separate pointer-socket messages for movement, click, and scroll. That is a strong implementation lead for a compatible Cardputer Wi-Fi mode, but the ThinQ app's exact current payloads have not been captured on this C1.

There is a second, separate Bluetooth detail: a recent community protocol analysis of ThinQ 5.2.12500 on a newer webOS 24 TV describes a Bluetooth advertisement used for waking a TV, including `com.webos.service.asc/registerDevice`, manufacturer data, and service UUID `FEBA`. Treat this as a possible **ThinQ wake** mechanism, not the Magic Remote's HID/M-RCU link. It has not been verified on this webOS 6.5.3 C1.

### Recommended ThinQ-assisted checks

1. Open ThinQ's TV remote and try its pointer/touchpad, scroll, arrow keys, OK, and text entry. Note which screens accept pointer input and whether arrow use hides the cursor.
2. Use the Cardputer's SSAP pointer socket to reproduce those same visible results. This validates a valuable feature without a physical Magic Remote.
3. If we later want TV wake while in standby, separately test the ThinQ BLE wake path. Do not infer that success means the Cardputer is emulating the Magic Remote.
4. Do not export or share the ThinQ client key, account tokens, or phone Bluetooth identifiers; none of these replace the M-RCU BLE capture.

### Capture ThinQ-normalized input events on this C1

The TV currently exposes `/dev/input/event10` as `LGE Network Input`, with keyboard, relative-pointer, and absolute-pointer capabilities. This is the most useful capture point for ThinQ actions on this rooted set: it records the events webOS receives after network input decoding, without trying to decrypt the app's TLS session. The node was checked read-only on 2026-11-17. The TV has `hexdump` and `timeout`; `evtest` is not installed.

Run this from the workstation, then operate ThinQ for 30 seconds while it runs:

```sh
ssh -F ~/.ssh/config lgtv "timeout 30 hexdump -v -e '1/8 \"sec=%u \" 1/8 \"usec=%u \" 1/2 \"type=%u \" 1/2 \"code=%u \" 1/4 \"value=%d\\n\"' /dev/input/event10"
```

The synthetic 24-byte record format was checked against the TV's `hexdump`. Each output row represents one Linux `input_event` with timestamp, type, code, and value. Interpret `type=1` as key/button (`value=1` down, `0` up, `2` repeat), `type=2` as relative pointer/wheel, `type=3` as absolute axes; `type=0` is sync framing and can be ignored. Relative codes `0` and `1` are X and Y; code `8` is the vertical wheel. Linux key/button codes can be decoded after capture.

Capture one labeled action at a time: pointer move, tap/click, scroll up/down, each arrow, OK, Back, Home, volume, and a harmless text entry. Note which screen/app is open and the action label with each run. This gives us a dependable action-to-event map for the Wi-Fi/ThinQ-compatible remote. It still does not reveal BLE packets or Magic Remote sensor reports.

## Capture and validation plan

1. **Confirm exact remote model.** Read the label inside the battery compartment; likely `MR21GA`, regulatory `BEJMR21GA`, but this has not been observed directly.
2. **Set up an over-the-air BLE capture.** Nordic's nRF Sniffer for BLE feeds Wireshark through a compatible Nordic dongle. Capture an ordinary fresh pairing between the physical MR21GA and the C1, then button, wheel, movement, idle, and voice-button phases. A host adapter's `btmon` capture only sees that host's Bluetooth controller; it cannot passively capture the TV-to-remote link.
3. **Save the GATT database and full report map.** Record service/characteristic UUIDs, HID Information, Report Map, Report Reference descriptors, Protocol Mode, boot/report mode, CCCD setup, security level, and any feature/output reports. Resolve the 20-vs-30-byte conflict from actual values and notifications.
4. **Map input semantics.** Record known button press/release patterns, counter behavior, wheel direction, update rate, motion axes, endian/scaling, idle packets, wake-on-motion behavior, and whether the report is encrypted after pairing. Avoid publishing bond keys or private identifiers.
5. **Test generic BLE HID first.** Pair a temporary generic mouse identity from Cardputer to the C1; verify relative movement/click and confirm that the physical Magic Remote still works. Remove only that temporary pairing afterward.
6. **Test native emulation on the TV only after descriptor/capture review.** Use a distinct local random BLE address and a unique name. Keep IR and the original remote available. Do not send unverified feature writes or invoke the LG re-pair shortcut until ready to recover.
7. **Compare pointer behavior.** Capture or sample pointer/sensor values using the official webOS MRCU API where available, then tune BMI270 orientation, gyro bias/scale, filtering, wake threshold, and pointer speed. Official docs list `coordinate`, `gyroscope`, `acceleration`, and `gameRotationVector`; the newer `sensor2` methods are documented for webOS 24+, so availability on this C1 must be checked separately.

## Technical constraints and design decisions

- ESP32-S3 supports BLE (not Bluetooth Classic). MR21GA BLE certification makes that a match, subject to the HOGP/capture confirmation.
- M5Stack documents the ADV's BMI270, IR emitter, 8 MB flash, and BLE operating current. The IMU is well suited to a native raw-sensor report if axes and calibration can be matched.
- Keep transports independent: `MRCU BLE`, `generic BLE HID`, `SSAP + pointer socket`, and `IR`. A failure in native pairing must not block the practical remote.
- Use a ring buffer for IMU samples and BLE notifications at the measured cadence. Avoid blocking Wi-Fi TLS work in the high-rate BLE/motion loop.
- Do not claim a true clone until the C1 accepts the native peripheral, pointer wakes and tracks as expected, the wheel and all buttons map correctly, and reconnect/re-pair behavior survives power cycles. Voice and standby wake should be reported as separate pass/fail capabilities.

## Evidence and references

- Local device facts: [device map](../device-map.md), collected read-only from the TV on 2026-11-16.
- LG [2021 MR21GA Magic Remote guide](https://www.lg.com/us/support/pdf/remotes/2021-Magic-Remote-MR21GA.pdf) describes Bluetooth, pointer operation, IR, wheel pairing, and Home+Settings re-pair.
- LG [Magic Remote API reference](https://webostv.developer.lge.com/develop/references/magic-remote) describes webOS-side coordinate and motion data. New `sensor2` methods are marked webOS 24+.
- LG [Bluetooth input-device pairing steps](https://www.lg.com/us/support/help-library/how-to-connect-a-bluetooth-device-to-your-lg-tv-CT10000018-20155394274758) list keyboard/mouse/gamepad pairing for webOS 6+.
- LG [ThinQ setup and remote functions](https://www.lg.com/us/support/help-library/lg-tv-how-to-set-up-the-lg-thinq-app-on-your-lg-smart-tv--20152745625356) documents pointer, scroll, arrows, OK, volume, channel, and input controls; [ThinQ feature guide](https://www.lg.com/us/support/help-library/lg-tv-thinq-features-and-settings--20152601841487) describes the in-app virtual remote.
- FCC/HCT [MR21GA Bluetooth LE test report](https://manuals.plus/m/fb6d429a3d25d953f864388e69f009f5d9ee5632f22c3615f3eb4c00806bd839.pdf) identifies the remote model and BLE test; LG's confidential filing request lists operational description, block diagram, schematics, and parts list as restricted, explaining why public filing material does not provide the full protocol.
- Community reverse-engineered Linux implementation: [brainrom/lg-magic README](https://github.com/brainrom/lg-magic/blob/master/README.md) and [report parser](https://github.com/brainrom/lg-magic/blob/master/kernel/lg_magic_main.c). Tested as MR20 according to its README; it does not prove MR21GA/C1 compatibility.
- Cardputer-specific comparison: [raptyk/IP9LGRemote](https://github.com/raptyk/IP9LGRemote), particularly its [protocol notes](https://github.com/raptyk/IP9LGRemote/blob/main/docs/PROTOCOLS.md). It implements network pointer control, IR, and generic BLE HID but says native Magic Remote pairing is not implemented.
- Additional reverse-engineered SSAP and ThinQ wake notes (tested on a newer webOS 24 TV, not the C1): [VesselWave/wave-remote protocol notes](https://github.com/VesselWave/wave-remote/blob/main/docs/PROTOCOL.md).
- M5Stack [Cardputer ADV hardware documentation](https://docs.m5stack.com/en/core/Cardputer-Adv).
- Espressif [ESP32-S3 Bluetooth guide](https://docs.espressif.com/projects/esp-idf/en/v5.2.2/esp32s3/api-guides/bluetooth.html) and official [BLE HID device example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/esp_hid_device).
- Nordic [nRF Sniffer for BLE overview](https://docs.nordicsemi.com/r/bundle/nrfutil/page/nrfutil-ble-sniffer/wireshark_sniffer_overview.html).
