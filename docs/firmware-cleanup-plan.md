# Firmware Cleanup Plan

## Current Product Shape

Biscuit on M5Paper is now treated as an e-ink reader and offline-first learning
device, not as the full legacy multipurpose/pentest dashboard.

Core flows:

- Reader: open books, recent books, OPDS, reading stats.
- Study: StudyPacks, device sync, file transfer.
- Network: WiFi join/transfer plus lightweight diagnostics needed for sync.
- Tools: a small set of low-risk utilities that work well on e-ink.
- System: settings and diagnostics.

Everything else is parked until there is a concrete use case.

## Kept In The Menu

Reader:

- Open Book
- Recent Books
- Reading Stats
- OPDS Browser

Study:

- Study Packs
- Device Sync
- WiFi Transfer

Network:

- WiFi Connect
- WiFi Transfer
- WiFi Scanner
- Host Scanner
- mDNS Browser
- Ping

Tools:

- Etch A Sketch
- QR Generator
- Calculator
- Clock
- Countdown

System:

- Settings
- Device Info
- Task Manager
- Battery

## Parked

The source files still exist, but these are no longer primary product surface:

- Recon/offense/defense/comms categories.
- BLE experiments.
- Security/pentest demos.
- Games.
- Legacy CSV Flashcards.
- Password/TOTP/stego/medical/emergency experiments.
- Tracking/geofence/vehicle/transit experiments.

These should only come back through a concrete product requirement and a
stability review.

## Architecture Problems Found

- `AppsMenuActivity` used to duplicate the entire app registry in two switch
  statements, one for radar mode and one for grid mode.
- The app menu had become the boot home screen, bypassing the simpler
  `HomeActivity` that already supports recents, browse, file transfer, apps and
  settings.
- The radar renderer assumed exactly 8 menu nodes.
- `HomeActivity` still created legacy pentest directories on SD card startup.

## First Cleanup Pass

- `ActivityManager::goHome()` returns to `HomeActivity` again.
- `AppsMenuActivity` is reduced to 5 large categories.
- The category factory is shared between radar and normal navigation.
- Radar rendering accepts a variable node count.
- Startup SD directory creation is reduced to current reader/study/tool paths.

## Next Cleanup Passes

1. Move the app registry out of `AppsMenuActivity` into a small declarative
   registry type.
2. Add feature flags or source filters for parked apps so unused code stops
   compiling, not only stops appearing in the menu.
3. Replace virtual button assumptions with full-row touch hit targets.
4. Add a touch event router shared by home/menu/list screens.
5. Remove old translation keys and activities once the parked list is stable.
