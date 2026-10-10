# dsync

ESP32 Classic Bluetooth A2DP sink → PCM5102A → headphone jack.

Needs an **ESP32 with Classic BT** (not ESP32-S3). Toolchain: **ESP-IDF 6.1**.

Wiring and pin defaults: [docs/HARDWARE.md](docs/HARDWARE.md).

## Prerequisites

- ESP-IDF **6.1** installed (Espressif installer / EIM)
- ESP32 DevKit (or equivalent) wired per HARDWARE.md
- USB serial for flash/monitor

## Build and flash

```bash
. $HOME/.espressif/tools/activate_idf_v6.1.sh
cd /path/to/dsync
idf.py set-target esp32   # once per clean tree
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your port (`ls /dev/ttyUSB*` / `ttyACM*`). Quit monitor with `Ctrl+]`.

Optional: `idf.py menuconfig` → *dsync board* (GPIOs) and *dsync Bluetooth* (device name, SSP).

## Use

1. Power the board; onboard LED should blink slowly (**discoverable**).
2. On the phone: Bluetooth settings → pair **dsync** (SSP confirm, or PIN **1234** if SSP is off).
3. Play audio on the phone; set output to **dsync** if the OS asks.
4. Listen on the PCM5102A jack.

**LED**

| Pattern | Meaning |
|---------|---------|
| Slow blink (~1 Hz) | Discoverable, waiting for pair/connect |
| Solid on | Connected, idle |
| Fast blink | Playing |

Disconnect the phone (or forget the device) before pairing another source — while connected, dsync is not discoverable.

Laptops: after pairing, select **dsync** as the system sound output. Windows may list it under “Other devices” rather than Speakers until audio COD is added in firmware.

## Docs

- [VERSIONS.md](docs/VERSIONS.md) — product / toolchain pins
- [HARDWARE.md](docs/HARDWARE.md) — wiring, mode pins, reserved pins
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — components and audio path
- [ROADMAP.md](docs/ROADMAP.md) — SD, OLED, PCB
- [AGENTS.md](AGENTS.md) — agent guide (build, guardrails, conventions); skills under `.agents/skills/`
