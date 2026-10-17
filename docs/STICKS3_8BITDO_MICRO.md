# 8BitDo Micro (Keyboard mode) on M5Stack StickS3

Status: **experimental**, not tested on hardware yet.

The emulators read the keyboard like on the Cardputer. The 8BitDo Micro is used in
**Keyboard mode (switch on `K`)**, so it sends normal keys over Bluetooth.

## Button mapping

Set this in the **8BitDo Ultimate Software** (mobile app, Keyboard mode only):

| Micro button | Key to assign | Action in the emulators |
|---|---|---|
| D-pad | *Nothing, keep the default arrow keys* | Up / Down / Left / Right |
| A | `L` | Button A |
| B | `K` | Button B |
| Start | `1` | Start |
| Select | `2` | Select |
| L | `Q` | Zoom out |
| R | `W` | Zoom in |

Notes:

- Arrow keys are converted internally to the Cardputer direction keys
  (`;` up, `.` down, `,` left, `/` right), so nothing has to be assigned for the D-pad.
- `Q` / `W` are virtual keys. The shim turns them into `FN + ,` / `FN + /`,
  which is what the emulators expect for zoom.
- Other Micro buttons (X, Y, Star) are free. Ideas: `=` / `-` for volume, `]` / `[` for brightness.

## StickS3 hardware buttons

| Button | Action |
|---|---|
| Front (BtnA) | `L` (A button) and Enter. Hold 1 s = quit game |
| Side (BtnB) | `.` (down / next item in menus) |

## Where this lives in the code

- `lib_sticks3/M5CardputerShim/` replaces `lib/M5Cardputer` in the `m5stack-sticks3` env.
- `Keyboard_Class::injectHidKey(usage, pressed)` receives HID key events from the BLE client.

## TODO

- BLE scan test: check that the StickS3 sees the Micro (ESP32-S3 is BLE only, no Classic).
- BLE HID keyboard client (NimBLE) calling `injectHidKey()`.
- Storage without SD slot (external SD module or flash).
- Audio (ES8311) check.
