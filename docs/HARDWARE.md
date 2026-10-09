# dsync hardware

## Dev board (v0.1)

**ESP32 DevKit V1** + **PCM5102A** module → 3.5 mm jack. Status via onboard LED.

### Why these I2S GPIOs?

**26 / 22 / 25** are the Espressif `a2dp_sink` example defaults — not magic silicon pins. Any free output GPIO works; change via `idf.py menuconfig` → *dsync board*. Kept as-is so docs match the common example.

### v1 wiring (now)

| Role | DevKit GPIO | Level (firmware) | Notes |
|------|-------------|------------------|--------|
| I2S BCK | 26 | clock | → PCM5102A BCK |
| I2S LRCK / WS | 22 | clock | → LRCK / LCK |
| I2S DIN | 25 | data | ESP DOUT → module DIN |
| PCM5102A **FMT** | **27** | **0** | I2S format (not left-justified) |
| PCM5102A **XSMT** | **32** | **1** | unmute (0 = silent) |
| PCM5102A **DEMP** | **33** | **0** | de-emphasis off |
| PCM5102A **FLT** | **4** | **0** | normal-latency FIR |
| Status LED | 2 | — | onboard |
| GND | GND | — | common |
| Power | 3V3/5V | — | match module marking |
| Module **SCK** | — | leave open | internal MCLK on most boards |

Do **not** solder the FMT/XSMT/DEMP/FLT jumper pads to fixed H/L if you wire them to ESP GPIOs — the MCU sets the levels via `board::kPins.dac.apply_mode()` when audio opens.

### What FMT / XSMT / DEMP / FLT mean

These are PCM5102A digital mode pins (on GY-PCM5102 modules they appear as solder bridges H1–H4):

| Pin | Name | Low (0) | High (1) | We use |
|-----|------|---------|----------|--------|
| FMT | Format | **I2S** | Left-justified | **0** |
| XSMT | Soft mute | muted | **unmuted** | **1** |
| DEMP | De-emphasis @ 44.1 kHz | **off** | on | **0** |
| FLT | FIR filter | **normal latency** | low latency | **0** |

Wrong FMT → garbled audio. XSMT left low → **no sound** (classic trap).

### DevKit V1 notes

- Avoid GPIO6–11 (flash).
- Avoid UART0 (GPIO1/3) for I2S.
- GPIO34–39 are input-only (not usable for these outputs).
- GPIO32/33 are fine on DevKit V1 (often on the left header).

## Reserved for later (not wired in firmware yet)

| Role | Suggested GPIO | Notes |
|------|----------------|--------|
| OLED SPI SCLK | 18 | |
| OLED SPI MOSI | 23 | |
| OLED CS | 5 | |
| OLED DC | 21 | |
| OLED RST | 19 | |
| SD SPI SCLK | 14 | Or share SPI with OLED + 2× CS |
| SD MOSI | 13 | |
| SD MISO | 12 | Strapping — prefer 27 on PCB if needed (27 is FMT now on breadboard) |
| SD CS | 15 | |
| PCB LED BT | 16 | |
| PCB LED Play | 17 | |

On a custom PCB you can free 27/32/33/4 from DAC ctrl by hard-wiring those nets and remapping in Kconfig.

## BOM (breadboard)

- ESP32 DevKit V1
- PCM5102A breakout (+ headphones / amp on jack)
- DuPont wires for I2S + four mode pins
- USB cable for power/flash
