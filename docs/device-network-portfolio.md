# Biscuit Device Network Portfolio

Date: 2026-05-17

This document captures the current portfolio of development options after the first usable original
M5Paper bring-up. The goal is not pairwise integration between every device. The goal is a small
local device fabric where each device can participate according to its strengths.

## Current Assets

- Original M5Paper: stable enough as an e-ink reader, SD file device, WiFi transfer endpoint, and
  low-power dashboard candidate.
- Xteink default target: original full feature target, but currently ROM-constrained.
- Cardputer: likely best as a keyboard console, command surface, provisioning terminal, and portable
  shell for the device fabric.
- Stackchan: likely best as an ambient/expressive notifier, voice/status endpoint, and social UI.
- M5Stick-class joystick controller: likely best as a handheld remote/input device.
- Arduino Mega with WiFi and display: likely best as a wired-IO/sensor/display bridge, depending on
  its WiFi module and firmware surface.
- Arduino R3-like board with WiFi and LED matrix: likely best as a simple status, alert, or sensor
  node rather than a rich protocol endpoint.
- Mac + Apple Home: currently preferred home surface. Home Assistant exists in a VM but is not a
  favored dependency because of operational complexity.

## Selection Policy

Prefer options that are:

- local-first and cloud-independent;
- reversible and testable on one or two devices before broad adoption;
- not pairwise by design;
- tolerant of offline devices and e-ink refresh latency;
- small enough for M5Paper and older Arduino-class devices;
- able to bridge to Apple Home later without making Apple Home the core protocol;
- compatible with WiFi transfer and reader stability work already completed.

Do not make Home Assistant a required hub. It can be a bridge later, but the fabric should work
without it.

## Protocol Facts To Carry Forward

- ESP-NOW is an Espressif connectionless WiFi protocol. It can send data between WiFi devices
  without an access point. Current ESP-IDF documentation describes ESP-NOW v2.0 with up to 1470-byte
  payloads and v1.0 with 250-byte payloads. For compatibility, design control packets to fit in
  250 bytes and reserve larger packets for negotiated v2 peers.
- Arduino-ESP32 exposes ESP-NOW APIs and examples, but it still relies on WiFi mode/channel setup.
  A fabric design needs an explicit channel strategy, especially when a device is also joined to a
  normal WiFi network.
- Matter is IP-based over WiFi, Thread, or Ethernet. It is the cleanest route into Apple Home, but it
  has a heavier product/security/certification model and should be treated as an edge bridge, not the
  first internal protocol.

Primary references:

- Espressif ESP-NOW, ESP-IDF v5.5:
  https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/network/esp_now.html
- Arduino-ESP32 ESP-NOW API:
  https://docs.espressif.com/projects/arduino-esp32/en/latest/api/espnow.html
- Espressif ESP-Matter:
  https://documentation.espressif.com/esp-matter/en/latest/esp32/index.html
- Apple Matter support:
  https://developer.apple.com/apple-home/matter/

## Candidate Architecture Variants

### A. Biscuit Link Over WiFi

Shape: mDNS discovery, HTTP status endpoints, and WebSocket or Server-Sent Events for local commands
and notifications.

Best fit:

- M5Paper WiFi transfer page already creates a useful web surface.
- Cardputer can act as a keyboard/controller over HTTP/WebSocket.
- A Mac daemon can aggregate devices without requiring Home Assistant.
- Easy to debug from a browser and serial logs.

Weakest link:

- Requires all devices to be on the same WiFi network or use an AP mode handoff.

Stepping-stone value:

- Defines the versioned device identity, status, event, and command model that other transports can
  reuse.

### B. ESP-NOW Local Fabric

Shape: small broadcast/discovery/control packets over ESP-NOW, with optional unicast peers and a
WiFi gateway device.

Best fit:

- Joystick/controller to e-ink device.
- Low-latency local button/status messages.
- Simple sensor/status nodes that should not join WiFi.
- Devices that can tolerate small packets and eventual consistency.

Weakest link:

- Channel management and coexistence with normal WiFi. ESP-NOW is attractive for local device
  control, but it is not a replacement for file transfer, rich UI, or Apple Home integration.

Stepping-stone value:

- Can become the low-power/control-plane transport under the same Biscuit Link message model.

### C. Local Hub Daemon On Mac

Shape: a small service on the Mac that discovers devices, stores state, exposes a local dashboard,
and bridges transports.

Best fit:

- Avoids making Home Assistant mandatory.
- Can bridge WiFi devices, ESP-NOW gateway devices, Apple Shortcuts, MQTT, WebDAV, or future Matter.
- Good place for logs, book sync, device inventory, and automation rules that are too heavy for
  microcontrollers.

Weakest link:

- Mac availability. If the Mac is off, the core device-to-device subset should still function.

Stepping-stone value:

- Lets the hardware stay simple while we learn the domain model.

### D. MQTT Or NATS As Internal Bus

Shape: a local broker and topic model for telemetry, commands, and events.

Best fit:

- Many small devices.
- Logging and automation.
- Future compatibility with Home Assistant if it becomes useful again.

Weakest link:

- Requires a broker and a topic discipline. It is simple operationally compared with Home Assistant,
  but still another runtime.

Stepping-stone value:

- Good bridge target after the Biscuit Link message model exists.

### E. Matter/Apple Home Edge Bridge

Shape: expose selected stable capabilities to Apple Home through Matter or a bridge, while keeping
the internal fabric independent.

Best fit:

- Lights, sensors, switches, occupancy/presence, and a few clean home automation concepts.
- Apple Home as the visible control surface.

Weakest link:

- Too heavy for the first integration layer. Matter is a product-grade ecosystem path with security,
  commissioning, and memory implications.

Stepping-stone value:

- Keeps the Apple Home path available without letting it dictate the internal protocol.

### F. BLE Provisioning And Presence

Shape: BLE scan/provisioning/presence, not full BLE feature parity.

Best fit:

- Nearby-device discovery.
- Presence and RSSI experiments.
- Possibly provisioning credentials or pairing a controller.

Weakest link:

- BLE and WiFi share the radio and memory budget. On M5Paper, BLE should return as a measured
  scan-only probe before any richer BLE application is enabled.

Stepping-stone value:

- Useful as a complement to WiFi/ESP-NOW, but not a primary fabric.

## Recommended Portfolio

Near-term portfolio:

1. Stabilize M5Paper as a reliable reader and WiFi transfer endpoint.
2. Define `Biscuit Link` as a tiny versioned message model:
   - device identity
   - capabilities
   - status snapshot
   - event envelope
   - command envelope
   - error envelope
3. Implement Biscuit Link over existing WiFi HTTP/WebSocket first.
4. Add a Mac-local hub only after the device endpoints are useful without it.
5. Run an isolated ESP-NOW probe with two ESP32-class devices using the same message envelopes.
6. Re-enable BLE on M5Paper as scan-only measurement, not as a full feature return.
7. Consider Matter/Apple Home only for stable home concepts after the internal model is clear.

Updated priority after the first Biscuit Link endpoint work:

1. Secure the existing web server before expanding the API surface:
   - short-lived session token for HTTP, WebDAV, and WebSocket;
   - saved-WiFi sync as the normal path;
   - WPA2 hotspot as bootstrap or fallback only;
   - hotspot QR may include WiFi credentials, but HTTP access still requires the session token.
2. Pivot the first product use case toward an offline-first M5Paper study device:
   - Mac prepares and syncs study packs;
   - M5Paper stores decks locally and can run reviews offline;
   - M5Paper writes rotated review log segments;
   - Mac later imports review logs and sends acknowledgements.
3. Keep ESP-NOW and broader device fabric as follow-on experiments after the secure sync and study
   data model are usable.

See also: [Offline-First Study Sync](study-sync-offline-first.md).

Do not pursue now:

- Full Home Assistant dependency.
- Pairwise bespoke integrations.
- Matter directly on every device.
- Full BLE feature parity on M5Paper.
- Large file transfer over ESP-NOW.

## First Milestone Proposal

Milestone name: `Biscuit Link v0`.

Acceptance:

- Every participating device has a stable `device_id`, `device_type`, firmware version, and
  capabilities list.
- M5Paper exposes a local `/api/device`, `/api/status`, and `/api/events` or WebSocket equivalent
  while WiFi transfer is active.
- Cardputer or a browser can send a small command envelope to M5Paper and receive an acknowledgement.
- The same JSON envelope is small enough to be mapped to ESP-NOW later by using compact field names
  or a binary encoding.
- The M5Paper reader and WiFi file transfer flows remain stable.

Example envelope, intentionally transport-neutral:

```json
{
  "v": 0,
  "id": "m5paper-5c013b0db9b0",
  "type": "status",
  "seq": 42,
  "ts": 123456789,
  "body": {
    "battery": 84,
    "activity": "reader",
    "heap": 218000
  }
}
```

## Open Questions

- Which devices are ESP32-class and can run ESP-NOW directly?
- What exact board is the joystick controller shipped with Stackchan?
- Should the Mac hub be a small CLI/daemon first, or a web dashboard first?
- Which Apple Home concepts are actually worth exposing: presence, alerts, reader status, buttons,
  lights, scenes, or sensors?
- Do we need encrypted device-to-device control from the start, or is local trusted LAN acceptable
  for v0 commands?

## Next Actions

1. Commit the current M5Paper known-good firmware state.
2. Add a tiny `DeviceIdentity` model and status endpoint to the existing web server.
3. Add a manual protocol smoke test using a browser or `curl` against M5Paper.
4. Create a separate ESP-NOW lab target or sample app outside the stable M5Paper reader path.
5. Revisit BLE after the scan-only memory/radio probe is measured.
