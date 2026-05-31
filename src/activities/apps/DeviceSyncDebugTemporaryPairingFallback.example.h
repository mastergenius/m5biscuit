#pragma once

// DEBUG-ONLY TEMPORARY FALLBACK.
//
// Copy this file to:
//   src/activities/apps/DeviceSyncDebugTemporaryPairingFallback.local.h
//
// The .local.h file is ignored by git. Use it only while proper QR/device pairing does not exist.
// This secret is a bootstrap key. The device uses it once to ask the host for a per-device sync
// secret, then stores that normal sync secret in /biscuit/sync/host.json on SD.
// A secret embedded into firmware can be extracted from a firmware image or flash dump.

#define BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_HOST_URL "http://<host-ip>:8787"
#define BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_DEVICE_ID "m5paper-example"
#define BISCUIT_DEVICE_SYNC_DEBUG_TEMPORARY_FALLBACK_SECRET_HEX \
  "0000000000000000000000000000000000000000000000000000000000000000"
