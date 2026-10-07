# dsync

ESP32 Bluetooth A2DP sink → I2S DAC (PCM5102A) → headphone jack.

**v0.1** targets an **ESP32 DevKit V1** with ESP-IDF **6.1**, onboard LED status, no SD/OLED yet.

## Docs

- [VERSIONS.md](docs/VERSIONS.md) — product / toolchain pins
- [HARDWARE.md](docs/HARDWARE.md) — wiring and reserved pins
- [ARCHITECTURE.md](docs/ARCHITECTURE.md) — components and audio path
- [ROADMAP.md](docs/ROADMAP.md) — SD, OLED, PCB

## Build

```bash
. $HOME/.espressif/v6.1/esp-idf/export.sh
idf.py set-target esp32
idf.py build
idf.py -p PORT flash monitor
```

Pair the phone with device name **dsync**, play audio, listen on the PCM5102A jack.

## Wire (defaults)

| Signal | GPIO |
|--------|------|
| I2S BCK | 26 |
| I2S LRCK | 22 |
| I2S DIN | 25 |
| LED | 2 (onboard) |
