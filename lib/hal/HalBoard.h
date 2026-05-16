#pragma once

#ifndef BISCUIT_BOARD_M5PAPER
#define BISCUIT_BOARD_M5PAPER 0
#endif

#if BISCUIT_BOARD_M5PAPER
#define BISCUIT_BOARD_XTEINK 0
#define BISCUIT_HAS_NATIVE_USB 0
#define BISCUIT_HAS_TOUCH 1
#define BISCUIT_HAS_PSRAM 1
#define BISCUIT_HAS_BLE 0
#define BISCUIT_BOARD_NAME "M5Paper"
#define BISCUIT_DISPLAY_WIDTH 960
#define BISCUIT_DISPLAY_HEIGHT 540
#else
#define BISCUIT_BOARD_XTEINK 1
#define BISCUIT_HAS_NATIVE_USB 1
#define BISCUIT_HAS_TOUCH 0
#define BISCUIT_HAS_PSRAM 0
#define BISCUIT_HAS_BLE 1
#define BISCUIT_BOARD_NAME "Xteink"
#endif

namespace BiscuitBoard {
inline constexpr bool isM5Paper() { return BISCUIT_BOARD_M5PAPER != 0; }
inline constexpr bool hasNativeUsb() { return BISCUIT_HAS_NATIVE_USB != 0; }
inline constexpr bool hasTouch() { return BISCUIT_HAS_TOUCH != 0; }
inline constexpr bool hasPsram() { return BISCUIT_HAS_PSRAM != 0; }
inline constexpr bool hasBle() { return BISCUIT_HAS_BLE != 0; }
inline constexpr const char* boardName() { return BISCUIT_BOARD_NAME; }
}  // namespace BiscuitBoard
