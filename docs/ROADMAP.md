# dsync roadmap

Preferred order after v0.1 (BT → jack):

| Version | Focus |
|---------|--------|
| **0.1** | A2DP sink → PCM5102A, DevKit V1 onboard LED |
| **0.2** | SD card (FatFS) + local playback into same `audio_out` |
| **0.3** | SPI 0.96" OLED status / browse (pins in [HARDWARE.md](HARDWARE.md)) |
| **0.4** | Custom PCB: extra LEDs, final pinout, cleaner analog for DAC |
| Later | Optional AAC (`a2dp_sink_stream_aac`), DAC swap, hardware volume |

Pin reserves for OLED/SD/PCB LEDs are documented now; firmware stays out of those until the matching milestone.
