# Distraction-Free Thinking Machine

Date: 2026-05-20

This note captures the plan for using the original M5Paper as a quiet offline writing and thinking
device with an external keyboard and one-button sync to the Mac agent environment.

## Product Shape

M5Paper should act like a better AlphaSmart-style device:

- local-first note capture;
- e-ink reading and writing surface;
- external keyboard input, starting with Logitech K380 / Bluetooth Classic HID probe;
- English/Russian UTF-8 text;
- explicit, low-friction sync to the Mac host;
- no dependency on a cloud service or always-on web editor.

The device is not intended to become a full editor. The first useful version should optimize for
capture and continuation:

- create a new note;
- continue the last note;
- open a recent note;
- append and lightly edit the current buffer;
- autosave reliably;
- sync notes to the Mac.

The Mac agent environment owns richer processing: indexing, concept extraction, linking, study-pack
generation, long-form editing, and conversion into durable knowledge artifacts.

## Local Note Model

Target SD layout:

```text
/biscuit/notes/
  notes/
    <note_id>.md
  meta/
    <note_id>.json
  journal/
    <note_id>.jsonl
  sync_state.json
```

`notes/<note_id>.md` is the user-readable artifact. It should be valid UTF-8 Markdown.

`meta/<note_id>.json` stores device-owned metadata:

- note id;
- title;
- created sequence;
- updated sequence;
- created monotonic time;
- updated monotonic time;
- optional trusted wall-clock timestamps;
- dirty/synced state;
- source device id.

`journal/<note_id>.jsonl` is the crash-safety layer. The editor may append small operations or
periodic snapshots there and compact them into the Markdown file when safe. The first implementation
can use coarse snapshots instead of a full operation log.

`sync_state.json` stores the highest acknowledged device sequence and host acknowledgements. Sync
must not depend on wall-clock correctness.

## Keyboard Input Plan

Current `BleKeyboardActivity` makes Biscuit act as a BLE keyboard for another machine. The thinking
machine needs the inverse: M5Paper as a HID host/client receiving reports from a physical keyboard.

The first target keyboard is Logitech K380. That matters because K380 is a Bluetooth Classic HID
keyboard, not a BLE-only keyboard. The current Arduino/PlatformIO `m5paper` stack has Bluetooth
enabled in the ESP32 SDK, but Classic HID Host is not compiled into the prebuilt libraries:

- `.pio/core/packages/framework-arduinoespressif32-libs/esp32/sdkconfig` has
  `# CONFIG_BT_HID_ENABLED is not set`;
- `libbt.a` contains the HID host object files, but the relevant objects have no exported symbols;
- `libesp_hid.a` exports the generic HID host and BLE HID host symbols, but not a usable Classic
  HID host implementation;
- Biscuit's `m5paper` board profile currently keeps `BISCUIT_HAS_BLE` disabled to protect the
  stable reader/study build.

Result: K380 cannot be supported by adding only application code to the current `m5paper` firmware.
We need a separate measured build stack/profile where Classic Bluetooth HID Host is actually enabled,
or a non-autonomous bridge where the Mac receives keyboard input and forwards text/events to M5Paper.

External keyboard support should therefore be introduced as a separate measured build/profile, for
example `m5paper-keyboard`, instead of silently re-enabling all Bluetooth apps in the stable reader
build.

Implementation stages for an autonomous K380 path:

1. Classic HID Host build probe:
   - create an `m5paper-keyboard` build profile or ESP-IDF/Arduino-as-component variant;
   - enable `CONFIG_BT_HID_ENABLED=y`;
   - verify that `esp_bt_hid_host_init`, `esp_bt_hid_host_connect`, and callbacks link;
   - measure firmware size, heap impact, WiFi coexistence, and sleep/wake behavior.
2. K380 pairing probe:
   - scan for Bluetooth Classic devices;
   - identify the Logitech K380 by name/class/address;
   - connect through Classic HID Host;
   - handle pairing/passkey if the keyboard requires it;
   - subscribe to input reports;
   - print usage code and modifier state to serial logs;
   - exit cleanly and release BLE memory.
3. Keyboard input manager:
   - translate HID usage codes plus modifiers to UTF-8 text events;
   - support US English and Russian ЙЦУКЕН;
   - support layout switch, likely `Ctrl+Space` or `Alt+Shift`;
   - expose non-text keys: Enter, Backspace, arrows, Home, End, Esc.
4. Notes activity:
   - append-first editor;
   - buffered rendering, not one e-ink update per key;
   - autosave journal;
   - explicit save status;
   - graceful sleep and resume.
5. Mac sync:
   - import notes into the Mac agent workspace;
   - keep device-originated notes one-way in v0 to avoid conflicts;
   - add two-way edits only after conflict semantics are designed.

Bridge fallback:

- K380 pairs to the Mac as usual.
- A Mac helper forwards text/key events to M5Paper over WiFi or serial.
- This is much easier to build and useful for prototyping the note editor, but it is not the final
  offline distraction-free mode because the Mac remains in the input loop.

## Classic HID Host Build Spike

Status: build probe exists and compiles as `m5paper-keyboard-probe`.

Build command:

```sh
UV_CACHE_DIR=.pio/uv-cache pio run -e m5paper-keyboard-probe
```

The probe is intentionally separate from the stable `m5paper` firmware. Flashing it replaces the
reader app with a serial-only keyboard experiment until the normal `m5paper` firmware is flashed
again.

The actual spike shape is:

```ini
[env:m5paper-keyboard-probe]
platform = ${base.platform}
board = m5paper11
framework = arduino, espidf
```

For `framework = arduino, espidf`, this installed PIOArduino builder does not apply
`custom_sdkconfig` to the ESP-IDF project. The probe therefore uses root `CMakeLists.txt` with:

```cmake
set(SDKCONFIG_DEFAULTS "${CMAKE_SOURCE_DIR}/src/probes/sdkconfig.defaults")
```

The required Bluetooth flags live in `src/probes/sdkconfig.defaults`:

```text
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_BLE_ENABLED=y
CONFIG_BTDM_CTRL_MODE_BTDM=y
# CONFIG_BTDM_CTRL_MODE_BLE_ONLY is not set
# CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY is not set
CONFIG_BT_HID_ENABLED=y
CONFIG_BT_HID_HOST_ENABLED=y
CONFIG_BT_SDP_COMMON_ENABLED=y
```

`CONFIG_BT_HID_HOST_ENABLED=y` is the important extra flag for the host API. Without it, `libbt.a`
builds HID-related objects but does not export `esp_bt_hid_host_*` symbols.

`CONFIG_BTDM_CTRL_MODE_BTDM=y` is also required. With the default BLE-only controller mode, the
Classic HID Host code links but `esp_bt_controller_init()` cannot start a Classic-capable
controller.

Because the probe runs under Arduino-as-component, it must include `esp32-hal-bt-mem.h`. That header
sets Arduino's BT-library-in-use marker before `initArduino()`. Without it, Arduino releases BTDM
memory during startup and `esp_bt_controller_init()` fails with `ESP_ERR_INVALID_STATE`.

The ESP-IDF component manager is disabled for the probe to avoid pulling unrelated managed
components. The probe also uses `src/CMakeLists.txt` to compile only
`src/probes/k380_hid_probe.cpp` instead of the full application.

BTDM build result on 2026-05-23:

- total image size: 953400 bytes;
- flash use reported by PlatformIO: 953144 / 6553600 bytes, 14.5%;
- DRAM sections: 48576 bytes;
- IRAM sections: 112527 bytes, 85.85%.

Flash/test command:

```sh
UV_CACHE_DIR=.pio/uv-cache pio run -e m5paper-keyboard-probe -t upload --upload-port /dev/cu.usbserial-5B1F0123571
pio device monitor -p /dev/cu.usbserial-5B1F0123571 -b 115200
```

Expected probe flow:

1. Hold one of the K380 `1`/`2`/`3` keys until the LED blinks.
2. The probe scans Classic Bluetooth devices and only attempts to connect names matching K380.
3. Healthy probe status looks like `bt=1 scanning=1`, with `DISC_RES` entries for nearby Classic
   Bluetooth devices.
4. Pairing events, device names, addresses, and HID reports are printed as `[K380]` serial logs.
5. HID report data is currently raw hex only; text/layout decoding is the next step after pairing
   and report delivery are confirmed.

Do not merge this into the normal `m5paper` build until the probe answers these questions:

- Does the ESP-IDF/Arduino combined profile build with M5Unified, M5GFX, SdFat, WebServer and the
  current reader code?
- How much flash does it add compared with the stable `m5paper` build?
- How much internal heap remains after Bluetooth init, pairing, WiFi shutdown/restart and reader
  reopen?
- Can Bluetooth be fully deinitialized before WiFi transfer/device sync?
- Does deep sleep/wake still work on battery?

## Seamless Sync Goal

Target user flow:

1. Turn on M5Paper.
2. Press `Sync Now`.
3. M5Paper joins saved WiFi.
4. M5Paper finds the paired Mac host.
5. Notes, Study review logs, and device status sync.
6. New StudyPacks or small config updates are pulled from the Mac.
7. M5Paper shows a short result and returns to the previous activity.

No QR scan should be required in the normal path after initial pairing.

## Sync Architecture Options

### A. Mac Pulls From Device

Shape: M5Paper joins WiFi, starts a short-lived sync server, advertises through mDNS, and the Mac
pulls changes.

Pros:

- reuses existing token-protected HTTP and WebDAV infrastructure;
- easy to debug from a browser or CLI;
- close to current File Transfer.

Weakest link:

- the device exposes an inbound service on the LAN;
- seamless operation needs a persistent pairing secret or the QR step comes back;
- the Mac needs to notice that the device is available.

### B. Device Pushes To Mac

Shape: the Mac runs a small local sync daemon. M5Paper joins WiFi, discovers or remembers the Mac,
authenticates, and pushes changes out to the host.

Pros:

- best match for the one-button flow;
- M5Paper does not need to expose generic SD-card APIs;
- easier to keep the sync surface narrow: notes, study logs, packs, time, status;
- sync can be initiated from the device without QR.

Weakest link:

- requires a Mac daemon or local app to be running;
- macOS firewall and sleep behavior must be handled;
- first pairing must be done carefully.

### C. QR Per Sync

Shape: keep the current short-lived token model and make the UI faster.

Pros:

- safest with current implementation;
- no persistent trust state;
- works with a plain browser.

Weakest link:

- not seamless enough for daily capture;
- still forces phone/camera/manual URL handling.

### D. Always-On Device Server

Shape: M5Paper exposes a persistent API whenever it is awake.

Pros:

- convenient when it works.

Weakest link:

- wrong security default;
- more battery drain;
- more failure modes around sleep, WiFi, and stale auth.

This option should not be used as the default architecture.

## Recommended Direction

Use a hybrid model:

1. Pair once through the existing QR/token flow.
2. Store a per-host pairing secret on M5Paper and in the Mac agent environment.
3. Run normal one-button sync as device-initiated push to a Mac sync daemon.
4. Keep the existing device-hosted File Transfer as fallback and manual recovery.

The persistent pairing secret should authorize only the narrow sync APIs. It must not grant generic
read/write access to the SD card.

## Pairing Model

Initial pairing:

1. User opens `Pair Mac` on M5Paper.
2. M5Paper shows a QR URL containing a one-time pairing token.
3. Mac helper connects to the device, exchanges identity, and creates a shared host/device secret.
4. M5Paper stores:
   - host id;
   - host display name;
   - host sync URL or mDNS service name;
   - host public key or shared secret reference;
   - pairing creation sequence.
5. Mac stores:
   - device id;
   - device display name;
   - device secret;
   - last imported note/review sequence;
   - target inbox paths.

Normal sync:

1. M5Paper resolves `_biscuit-sync._tcp.local` or uses the saved host URL.
2. Mac returns a challenge.
3. M5Paper signs the challenge with the pairing secret.
4. Both sides derive a short-lived session token for this sync run.
5. M5Paper sends a manifest of dirty data.
6. Mac requests or accepts deltas, imports them, and returns acknowledgements.
7. M5Paper marks acknowledged sequences as synced.

## Host Discovery And Remote Access

M5Paper should not depend on "the Mac" as a special object. It pairs with one or more sync hosts.
Each host has:

- `host_id`: stable identity generated by the sync daemon;
- display name, such as `Geni MacBook`;
- preferred endpoint, such as `http://<host-ip>:8787`;
- optional mDNS service name, such as `Geni-MacBook._biscuit-sync._tcp.local`;
- pairing secret or public-key material;
- priority and last-success metadata.

Normal LAN discovery:

1. The host daemon advertises `_biscuit-sync._tcp.local`.
2. M5Paper browses or resolves the service.
3. If the advertised `host_id` matches a paired host, M5Paper syncs.
4. If mDNS fails, M5Paper tries the saved endpoint from the last successful sync.

This means the Mac does not need to find the device in the daily flow. The device finds a trusted
host and initiates the connection.

Other host machines:

- A Linux box, VM, or future small home server can run the same daemon and get its own `host_id`.
- M5Paper can support multiple paired hosts later, but v0 should keep one primary host to avoid
  conflict semantics.
- Moving the app to another machine is a re-pairing operation unless the host state directory and
  pairing secret are intentionally migrated.

Remote access from an office:

- The simplest supported model is still "sync to the home host". If the home Mac runs the daemon,
  M5Paper syncs to it on the home LAN, and the user can SSH into that Mac from the office to inspect
  or operate the imported files.
- An SSH tunnel from the office to the home Mac helps the user reach the home daemon, but it does
  not by itself make M5Paper able to reach an office laptop. M5Paper can only connect to endpoints
  reachable from its own network.
- If the sync application must run outside the home LAN, use a reachable endpoint profile: VPN,
  reverse tunnel, home relay host, or a manually configured DNS/IP endpoint. That should be an
  advanced mode, not the v0 default.

Discovery priority:

1. Paired mDNS service with matching `host_id`.
2. Last-known direct URL for the paired host.
3. Manually configured host URL from device settings or SD config.
4. QR/File Transfer fallback.

The discovery result is only a routing hint. Authentication always comes from the pairing secret.

Fallback:

- if the Mac host is not found, show `Mac not found` plus `Open QR fallback`;
- if auth fails, require re-pairing;
- if WiFi fails, offer saved-WiFi retry or hotspot.

## Sync Scope

Device to Mac:

- notes and note metadata;
- Study review log segments;
- crash/panic summaries if present;
- battery, firmware, storage, and memory snapshot;
- optional reader progress later.

Mac to device:

- StudyPacks;
- Study log acknowledgements;
- Mac-provided time with explicit clock trust;
- small settings/config updates that are safe to apply offline;
- optional note updates only after conflict handling exists.

Out of scope for v0:

- arbitrary SD-card sync;
- always-on background sync;
- two-way note editing;
- cloud relay;
- Bluetooth file transfer.

## Security Invariants

- No unauthenticated write endpoint.
- No persistent generic SD-card capability.
- mDNS discovery is not authorization.
- Pairing secrets are local machine/device secrets and must not be committed to the repo.
- `DeviceSyncDebugTemporaryPairingFallback.local.h` is a debug-only temporary fallback while QR
  pairing does not exist. It must stay ignored, must be device-specific, and must be removed once
  real pairing and secret rotation work.
- Firmware-embedded fallback secrets are not network-visible in normal challenge/HMAC sync, but
  they can be extracted from a firmware image or flash dump.
- Current HTTP sync authenticates payloads but does not encrypt them. Treat it as trusted-LAN only
  until HTTPS/noise-style encryption or a local VPN transport exists.
- Each sync run uses a short-lived session token derived from the pairing secret.
- Device UI must show which host it is syncing with.
- A physical action on M5Paper starts sync.
- Host pairing can be revoked on device.
- The existing File Transfer token model remains the manual escape hatch.

## Mac Agent Shape

Near-term Mac helper:

```text
agents/device-sync/
  README.md
  biscuit-sync
  config/
  inbox/
    notes/
    study-logs/
    device-status/
```

It can later merge with Study Twin tooling, but the sync substrate should not be study-specific.
StudyPacks and notes are different payloads over the same trust and transport layer.

The first Mac runtime can be a CLI daemon:

```text
biscuit-sync serve
biscuit-sync pair <device-url>
biscuit-sync status
biscuit-sync import-once
```

Later this can become a small menubar app or local web UI.

## Acceptance Criteria

v0 is done when:

- M5Paper can pair with one Mac host once;
- after pairing, `Sync Now` works without QR on a trusted home LAN;
- notes and Study review logs arrive on the Mac;
- StudyPack acknowledgements and Mac time sync return to the device;
- no data is lost if the Mac is unavailable;
- sync failure produces a clear on-device result;
- unpaired clients cannot read or write sync data;
- File Transfer still works as manual fallback.

## Implementation Plan

1. Document this architecture and link it from the device portfolio and study sync notes.
2. Add a Mac `biscuit-sync serve` skeleton with local inbox paths and a narrow HTTP API.
3. Add one-time pairing over the existing token-protected device server.
4. Add M5Paper `Sync Now` activity:
   - done: ensure saved WiFi;
   - done: use a saved paired host endpoint from `/biscuit/sync/host.json`;
   - done: authenticate with challenge/HMAC;
   - done: push a first device status payload;
   - next: discover paired host through mDNS;
   - next: exchange manifests;
   - next: sync Study logs first;
   - sync notes when NotesActivity exists.
5. Add dedicated device-side sync state for host acknowledgements.
6. Add BLE HID keyboard probe.
7. Add NotesActivity and local note storage.
8. Extend sync to notes.
9. Add revocation, diagnostics, and a small Mac UI only after the CLI path is reliable.
