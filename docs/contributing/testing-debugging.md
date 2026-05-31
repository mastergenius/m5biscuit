# Testing and Debugging

Biscuit runs on real hardware, so debugging usually combines local build checks and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## Flash and monitor

Flash firmware:

```sh
pio run --target upload
```

Open serial monitor:

```sh
pio device monitor
```

Optional enhanced monitor:

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

## Firmware Version Checks

The firmware base version lives in `platformio.ini` as `[biscuit] version` and uses CalVer:

```text
YYYY.MM.DD.N
```

Development builds append the PlatformIO environment, branch, short git SHA, and a dirty marker when
the worktree has uncommitted changes:

```text
2026.05.31.1-m5paper+master.771fbaf
2026.05.31.1-m5paper+master.771fbaf.dirty
```

Check an attached M5Paper over USB serial:

```sh
scripts/device-command status
```

The device is running the latest committed firmware when the reported `firmware` field has the same
short SHA as `git rev-parse --short=7 HEAD` and does not end in `.dirty`.

## Useful bug report contents

- Firmware version and build environment
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing `.crosspoint/` cache on SD card

## Common troubleshooting references

- [User Guide troubleshooting section](../../USER_GUIDE.md#7-troubleshooting-issues--escaping-bootloop)
- [Webserver troubleshooting](../troubleshooting.md)
