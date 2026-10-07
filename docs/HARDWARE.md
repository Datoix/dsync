# dsync hardware

## Dev board (v0.1)

**ESP32 DevKit V1** + **PCM5102A** module → 3.5 mm jack. Status via onboard LED.

### v1 wiring (now)

| Role | DevKit GPIO | Notes |
|------|-------------|--------|
| I2S BCK | 26 | |
| I2S LRCK / WS | 22 | |
| I2S DIN → DAC | 25 | |
| Status LED | 2 | Onboard LED (active-high) |
| GND / 3V3 | — | Common with PCM5102A |

Pins are Kconfig (`CONFIG_DSYNC_*`) in the `board` component — remappable for PCB.

### PCM5102A module

- Use **BCK**, **LRCK/LCK**, **DIN** only; leave **SCK** open (internal MCLK on most boards).
- Jumpers (typical GY-PCM5102 pads): **FMT=L** (I2S), **XSMT=H** (unmute), **DEMP=L**, **FLT=L**.
- Muted **XSMT** is the usual “no sound” cause.

### DevKit V1 notes

- Avoid GPIO6–11 (flash).
- Avoid UART0 flash pins (GPIO1/3) for I2S.
- GPIO34–39 are input-only.

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
| SD MISO | 12 | Strapping — prefer 27 on PCB if needed |
| SD CS | 15 | Strapping — prefer 27 on PCB if boot issues |
| PCB LED BT | 16 | |
| PCB LED Play | 17 | |
| PCB LED Error | 4 | Optional |

## BOM (breadboard)

- ESP32 DevKit V1
- PCM5102A breakout (+ headphones / amp on jack)
- USB cable for power/flash
