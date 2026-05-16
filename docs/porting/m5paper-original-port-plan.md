# M5Paper Original Port Plan

Date: 2026-05-16

Target: original M5Stack M5Paper / Paper v1.1, not M5Paper S3.

## Goal

Make Biscuit run on original M5Paper while preserving the existing Xteink X4/X3 firmware path.
The first usable target is a black-and-white build with boot, display, storage, input, sleep,
WiFi/BLE utilities, and reader workflows working on device. Native USB HID and USB mass-storage
are explicitly out of scope for original M5Paper because the board uses a USB-to-serial bridge,
not native USB device support.

## Current Observations

- `platformio.ini` is an X4/X3 build: board is `esp32-c3-devkitm-1`, C3 USB flags are enabled,
  and hardware libraries are symlinked from `open-x4-sdk` (`BatteryMonitor`, `InputManager`,
  `EInkDisplay`, `SDCardManager`).
- `HalDisplay` directly includes and owns `EInkDisplay`, so display support is not yet pluggable.
- `HalGPIO` directly includes `InputManager`, defines Xteink EPD/SPI pins, assumes seven button
  indices, and implements X3/X4 detection rather than a general board profile.
- `HalSystem` contains RISC-V panic-stack capture copied from ESP32-C3 panic internals. Original
  M5Paper is Xtensa ESP32, so this code cannot be used unchanged.
- `GfxRenderer` assumes a landscape physical panel and rotates `480x800` portrait coordinates onto
  an `800x480` panel. Original M5Paper is a native `540x960` portrait panel.
- Image upload/conversion UI and tests contain several `480x800` assumptions that must become
  runtime geometry values.

## Target Hardware Facts

M5Stack documentation lists original Paper as ESP32-D0WDQ6-V3, 520KB SRAM, 16MB flash, 8MB PSRAM,
540x960 4.7 inch e-ink, 16 grayscale levels, IT8951E display controller, GT911 touch, BM8563 RTC,
TF-card storage, and dial switch pins on GPIO37/GPIO38/GPIO39/GPIO2.

References:

- https://docs.m5stack.com/en/core/m5paper
- https://github.com/m5stack/M5EPD

The legacy `M5EPD` repository now recommends `M5GFX` and `M5Unified`. The port should start with
M5GFX/M5Unified unless a compile or hardware blocker forces the legacy M5EPD driver.

## Problem Frame

Type: synthesis/port. The task is not to optimize an existing M5Paper build; it is to combine the
current Biscuit app stack with a different board, display controller, power model, and input model.

Acceptance criteria:

- `pio run -e m5paper` builds without breaking `pio run -e default`, `pio run -e slim`, or native
  tests.
- M5Paper boots to the Biscuit home UI and can refresh the display reliably.
- SD card mount/read/write works under the same `HalStorage` API used by apps.
- The core logical input set works: Back, Confirm, Left, Right, Up, Down, Power/PageTurn.
- Sleep, wake, charging/battery status, and RTC behavior are implemented or clearly degraded with
  user-visible fallback.
- Reader flows, file browser, settings, WiFi scan/connect, BLE scan, and at least one simple game
  have been smoke-tested on real M5Paper hardware.
- Native USB-only activities are hidden or disabled on M5Paper builds.

Non-negotiable constraints:

- Do not regress X4/X3 builds.
- Keep app code behind `HalDisplay`, `HalGPIO`, `HalStorage`, `HalPowerManager`, and
  `MappedInputManager` wherever possible.
- Do not introduce a touch-only UX path; M5Paper touch may augment navigation, but every required
  workflow must remain reachable through logical buttons or a documented fallback.
- First milestone is black-and-white. Native 16-level grayscale can be a later phase after the
  driver and geometry are stable.

## Blockers And Gates

1. Build dependencies are board-specific.
   The current `lib_deps` points to Open X4 SDK symlinks. M5Paper needs separate dependencies and
   conditional includes. Gate: create `env:m5paper` and compile a minimal HAL without affecting X4.

2. Display driver choice must be proven early.
   M5GFX/M5Unified is preferred because M5EPD is deprecated, but the existing renderer expects
   direct ownership of a 1bpp framebuffer. Gate: prove that the chosen driver can accept a Biscuit
   1bpp buffer or a low-cost conversion path from 1bpp to M5Paper output.

3. Geometry assumptions are structural.
   Current orientation math treats physical display as `800x480`. M5Paper is `540x960`, so a naive
   `DISPLAY_WIDTH` replacement will rotate or clip output. Gate: add a board/display geometry model
   and verify a test pattern in all four orientations.

4. Input model mismatch.
   X4 has seven buttons; M5Paper has three dial/button inputs plus touch. Gate: define a stable
   M5Paper input policy before porting all apps. Suggested default: physical left/right/middle map
   to Left/Right/Confirm, long-middle or power maps to Back/Power, and touch zones provide Up/Down
   and Back where needed.

5. Power and wake semantics differ.
   X4 sleep uses GPIO/power latch behavior. M5Paper uses BM8563/board power APIs and cannot power
   off on USB in the same way. Gate: implement M5Paper sleep with M5Unified or BM8563 APIs and
   document any USB-powered behavior difference.

6. `HalSystem` is architecture-specific.
   The current panic wrapper references RISC-V exception frames. Gate: either guard it behind
   ESP32-C3-only macros or replace it with a portable fallback for Xtensa.

7. Native USB features are not portable.
   USB HID and USB mass-storage are stubs already, and original M5Paper does not expose native USB
   device mode through the CP2104 serial bridge. Gate: add capability flags so these apps are hidden
   on M5Paper.

8. Hardware validation is mandatory.
   Display refresh, sleep, touch/buttons, and battery status cannot be proven in native tests. Gate:
   keep a real-device smoke checklist and do not mark the port complete until it has been run.

9. Operational repository blocker observed in this session.
   The local shell cannot reach `api.github.com`, and the available GitHub connector does not expose
   a fork/create-repository operation. The code plan can be committed locally, but creating the
   remote fork requires either `gh repo fork yattsu/biscuit --clone=false` outside the sandbox or a
   connector/tool with repository fork permission.

## Compared Implementation Variants

Selection policy: prefer the option with the lowest long-term maintenance risk that keeps X4/X3
intact and proves display output quickly.

| Variant | Description | Strengths | Weakest Link | Verdict |
| --- | --- | --- | --- | --- |
| A. M5GFX/M5Unified HAL | Add a M5Paper HAL backend using maintained M5Stack libraries. | Best future maintenance, power/input APIs in one stack, avoids starting on deprecated library. | Need to prove 1bpp buffer push and e-paper refresh controls match Biscuit needs. | Preferred first probe. |
| B. Legacy M5EPD HAL | Use M5EPD directly for display, touch, power, and SD. | Closest examples for original M5Paper and IT8951. | Deprecated; likely older ESP-IDF/Arduino assumptions. | Fallback if M5GFX blocks. |
| C. Direct IT8951/GT911/BM8563 drivers | Implement board support without M5Stack abstraction. | Maximum control and least library magic. | Slowest path, highest hardware-debug risk. | Only if A/B fail. |

## Work Plan

### Phase 0: Repository And Baseline

- Preserve current upstream remotes before changing `origin`.
- Add a porting plan commit before code changes.
- Run `git submodule status`, `pio run -e native`, and, if dependencies are available,
  `pio run -e default` to record the starting point.
- Create `env:m5paper` as an isolated build target. Do not change `default_envs` initially.

### Phase 1: Board Capability Model

- Add a small board capability header, for example `lib/hal/HalBoard.h`.
- Define compile-time capabilities:
  - `BISCUIT_BOARD_XTEINK_X4`
  - `BISCUIT_BOARD_XTEINK_X3`
  - `BISCUIT_BOARD_M5PAPER`
  - `BISCUIT_HAS_NATIVE_USB`
  - `BISCUIT_HAS_TOUCH`
  - `BISCUIT_HAS_PSRAM`
  - `BISCUIT_DISPLAY_NATIVE_WIDTH`
  - `BISCUIT_DISPLAY_NATIVE_HEIGHT`
  - `BISCUIT_DISPLAY_NATIVE_ORIENTATION`
- Replace downstream `gpio.deviceIsX3()` / `deviceIsX4()` checks where they really mean geometry,
  input layout, grayscale behavior, or battery behavior.

### Phase 2: Build Target

- Add `[env:m5paper]` with classic ESP32 board settings. Likely starting board:
  `m5stack-core-esp32` or a custom board JSON if PSRAM/flash/partition settings require it.
- Add M5Paper-specific `lib_deps`: initially `m5stack/M5Unified` and `m5stack/M5GFX`.
- Remove C3 native USB flags from the M5Paper environment.
- Add `-DBISCUIT_BOARD_M5PAPER=1`, `-DBOARD_HAS_PSRAM`, and any required PSRAM build flags.
- Keep C3/X4 linker wrappers out of M5Paper until each wrapper is proven compatible.

### Phase 3: HAL Backend Split

- Split HAL implementations by board using narrow files rather than large `#ifdef` blocks:
  - `HalDisplay_xteink.cpp`
  - `HalDisplay_m5paper.cpp`
  - `HalGPIO_xteink.cpp`
  - `HalGPIO_m5paper.cpp`
  - `HalStorage_xteink.cpp`
  - `HalStorage_m5paper.cpp`
  - `HalPowerManager_xteink.cpp`
  - `HalPowerManager_m5paper.cpp`
- Keep public headers stable unless the current API is too X4-specific.
- For M5Paper storage, initialize SPI/SD with the documented TF-card pins:
  MISO GPIO13, MOSI GPIO12, SCK GPIO14, CS GPIO4.
- For M5Paper power, start with M5Unified power APIs. If unavailable, use BM8563 directly.

### Phase 4: Display And Renderer

- First implement a 1bpp framebuffer sized to M5Paper logical output. Target memory:
  `540 * 960 / 8 = 64800` bytes.
- Add a `DisplayGeometry` model to `HalDisplay`:
  - physical width/height
  - logical portrait width/height
  - whether physical coordinates are portrait-native or landscape-native
  - bytes per row
- Rework `GfxRenderer::getScreenWidth()`, `getScreenHeight()`, and coordinate rotation so native
  portrait panels do not use X4's `480x800 -> 800x480` transform.
- Implement a M5Paper display flush path:
  - 1bpp buffer to monochrome e-paper output for milestone 1.
  - Later optional conversion to M5Paper 4bpp grayscale.
- Verify with a border/corner/axis test pattern before testing full UI.

### Phase 5: Input Policy

- Implement `HalGPIO_m5paper` event state for the three physical buttons:
  - left: Left / PageBack
  - right: Right / PageForward
  - middle: Confirm
  - long middle: Back or menu escape
  - power behavior: mapped through board power API where available
- Add optional touch zones behind the same logical `MappedInputManager` API:
  - top/bottom zones for Up/Down
  - left/right zones for PageBack/PageForward
  - center tap for Confirm
  - corner tap or long press for Back
- Keep all app code using logical buttons; do not add direct touch handling to individual apps in
  the first milestone.

### Phase 6: UI And Layout

- Create M5Paper metrics for button hints, side hints, and content width.
- Remove hardcoded X4/X3 button hint positions from themes by deriving positions from screen width
  and capability profile.
- Update apps with fixed `480x800` constants:
  - `EtchASketchActivity`
  - `VoronoiActivity`
  - `MatrixRainActivity`
  - preview/native test renderers
  - web upload/conversion limits in `src/network/html/FilesPage.html`
- Update `DeviceInfoActivity` to report runtime display geometry and M5Paper board name.

### Phase 7: Feature Gating

- Add capability gates to app menus.
- Hide or disable on M5Paper:
  - USB HID
  - USB mass-storage
- Keep WiFi, BLE, ESP-NOW, and promiscuous-mode features initially enabled but verify ESP32 classic
  API compatibility one category at a time.
- Recheck raw WiFi TX/linker wrapper behavior on classic ESP32 before exposing active wireless
  testing features.

### Phase 8: Image Conversion And Reader Path

- Change default image target size from `display.getDisplayHeight()/getDisplayWidth()` assumptions
  to logical portrait dimensions.
- Update web file upload limits from hardcoded `480x800` to values emitted by firmware or compile
  time board config.
- Smoke-test EPUB/TXT page rendering with:
  - default portrait
  - inverted portrait
  - landscape modes
  - cover thumbnails
  - PNG/JPEG-heavy EPUBs

### Phase 9: Verification

Build verification:

- `pio run -e native`
- `pio test -e native`
- `pio run -e default`
- `pio run -e slim`
- `pio run -e m5paper`

Device smoke checklist:

- Boot splash/home render.
- Full refresh and fast refresh do not corrupt corners or axes.
- SD card mounts and file browser opens root.
- Settings can be opened and exited.
- Sleep/wake works on battery and USB with documented differences.
- Battery/charging indicator is plausible.
- WiFi scan completes and releases radio.
- BLE scan completes and releases radio.
- Reader opens a TXT and EPUB file from SD.
- One game and one utility are usable through M5Paper input policy.

### Phase 10: Documentation And Release

- Update README hardware table to say X4 remains primary and M5Paper original is experimental until
  smoke-tested.
- Add a M5Paper flashing section with the correct serial driver expectation.
- Document unsupported features on original M5Paper.
- Keep the first M5Paper release as experimental until at least one full battery/sleep cycle has
  been tested.

## Predictions To Verify

- If the HAL split is clean, X4/X3 default builds should remain within normal binary size variance
  and should not require app-level code changes for basic boot.
- If M5GFX can accept an external 1bpp conversion path, home screen output should be visible before
  any app-level layout changes.
- If the input policy is centralized in `HalGPIO`/`MappedInputManager`, most apps should become
  navigable without direct touch code.
- If layout constants are converted to renderer dimensions, the reader and file browser should
  scale to 540x960 with fewer changes than dashboard/game apps.

## First Pull Request Scope

The first PR should be intentionally small:

- Add `env:m5paper`.
- Add board capability definitions.
- Add M5Paper HAL stubs that compile.
- Guard C3-only panic code and native USB-only activities.
- Commit no app UX redesign beyond capability gates.

Do not attempt display, touch, power, storage, and all apps in one PR. The port should advance in
hardware-verifiable increments.
