/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* ESP32 Linker Script Memory Layout

   Local M5Paper override for Biscuit.

   The stock Arduino-ESP32 3.x ESP32 memory.ld caps iram0_0_seg at 0x20000.
   The original M5Paper uses classic ESP32 with WiFi/BLE enabled, and that
   combination needs a few KB more IRAM than the default linker assertion allows.

   SRAM1 starts at 0x400A0000 and is shared between IRAM and heap. The ESP-IDF
   script already subtracts any IRAM spill past _sram1_iram_start from
   _heap_end, so extending the linker region here preserves runtime accounting
   while avoiding a package-local patch under .pio/core.
*/

MEMORY
{
  /* Classic ESP32 IRAM for PRO CPU. The last 16KB may spill into SRAM1. */
  iram0_0_seg (RX) : org = 0x40080000, len = 0x20000 + 0x4000

  /* Flash-mapped executable code. */
  iram0_2_seg (RX) : org = 0x400D0020, len = 0x330000-0x20

  /* Shared data RAM, excluding memory reserved for ROM bss/data/stack. */
  dram0_0_seg (RW) : org = 0x3FFB0000 + 0xdb5c,
                                     len = 0x2c200 - 0xdb5c

  /* Flash-mapped constant data. */
  drom0_0_seg (R) : org = 0x3F400020, len = 0x400000-0x20

  /* RTC fast memory (executable). Persists over deep sleep. */
  rtc_iram_seg(RWX) : org = 0x400C0000, len = 0x2000 - (((0x10) + (8 - 1)) & ~(8 - 1))

  /* RTC fast memory, viewed from the data bus. */
  rtc_data_seg(RW) : org = 0x3ff80000, len = 0x2000 - (((0x10) + (8 - 1)) & ~(8 - 1))

  rtc_fast_reserved_seg(RW) : org = 0x3ff80000 + 0x2000 - (((0x10) + (8 - 1)) & ~(8 - 1)), len = (((0x10) + (8 - 1)) & ~(8 - 1))

  /* RTC slow memory (data accessible). Persists over deep sleep. */
  rtc_slow_seg(RW) : org = 0x50000000 + 512,
                                     len = 0x2000 - 512 - ((24))

  rtc_slow_reserved_seg(RW) : org = 0x50000000 + 0x2000 - ((24)), len = ((24))

  /* External PSRAM. */
  extern_ram_seg(RW) : org = 0x3F800000,
                                     len = 0x400000
}

_heap_start = _heap_low_start;
_sram1_iram_start = 0x400A0000;
_sram1_iram_len = ( _iram_end > _sram1_iram_start) ? (_iram_end - _sram1_iram_start) : 0;
_heap_end = ALIGN(0x40000000 - _sram1_iram_len - 3, 4);
_data_seg_org = ORIGIN(rtc_data_seg);

REGION_ALIAS("rtc_data_location", rtc_slow_seg );
REGION_ALIAS("default_code_seg", iram0_2_seg);
REGION_ALIAS("default_rodata_seg", drom0_0_seg);

ASSERT(_rodata_start == ORIGIN(default_rodata_seg),
       ".flash.appdesc section must be placed at the beginning of the rodata segment.")
