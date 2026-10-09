# dsync

ESP32 Bluetooth A2DP sink → I2S DAC (PCM5102A) → headphone jack.

**v0.1** targets an **ESP32 DevKit V1** with ESP-IDF **6.1**, onboard LED status, no SD/OLED yet.

## Docs

- [VERSIONS.md](docs/VERSIONS.md) — product / toolchain pins
- [HARDWARE.md](docs/HARDWARE.md) — wiring, PCM5102A mode pins, reserved pins
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — components and audio path
- [ROADMAP.md](docs/ROADMAP.md) — SD, OLED, PCB

## Build

```bash
. $HOME/.espressif/tools/activate_idf_v6.1.sh
idf.py set-target esp32
idf.py build
idf.py -p PORT flash monitor
```

Pair the phone with device name **dsync**, play audio, listen on the PCM5102A jack.

## Wire (defaults)

| Signal | GPIO | Notes |
|--------|------|--------|
| I2S BCK | 26 | Espressif example default |
| I2S LRCK | 22 | |
| I2S DIN | 25 | → module DIN |
| FMT | 27 | firmware drives **0** (I2S) |
| XSMT | 32 | firmware drives **1** (unmute) |
| DEMP | 33 | firmware drives **0** |
| FLT | 4 | firmware drives **0** |
| LED | 2 | onboard |

Leave module **SCK** unconnected. See [HARDWARE.md](docs/HARDWARE.md) for FMT/XSMT/DEMP/FLT meaning.
