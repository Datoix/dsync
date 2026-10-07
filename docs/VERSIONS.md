# dsync versions

| Item | Value |
|------|--------|
| Product | dsync **0.1** — Bluetooth A2DP sink → jack |
| Target SoC | ESP32 (`idf.py set-target esp32`) |
| Dev board | ESP32 DevKit V1 (WROOM-32) |
| ESP-IDF | **v6.1** (`idf.py --version`) |
| BT stack | Bluedroid Classic (BLE controller memory released) |
| Profiles | A2DP sink (+ connection/audio state for UI) |
| Codec path | Phone SBC → stack PCM → I2S TX |
| DAC | PCM5102A |
| Display / SD | Deferred (see [ROADMAP.md](ROADMAP.md)) |

## Reference

- Espressif example pattern: `examples/bluetooth/bluedroid/classic_bt/a2dp_sink_stream` (IDF 6.x)
