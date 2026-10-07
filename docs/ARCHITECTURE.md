# dsync architecture

## Layout

```
main/                     app_main — compose owners
components/
  board/                  Kconfig pin getters
  idf_handles/            unique_ptr wrappers for IDF/FreeRTOS C handles
  audio_out/              I2S TX + ringbuffer + i2s_wr task
  bt_sink/                Classic Bluedroid A2DP sink
  ui_leds/                status LED (atomic status + periodic timer)
```

Namespaces: `dsync::board`, `dsync::handles`, `dsync::audio`, `dsync::bt`, `dsync::ui`.

`app_main` owns `Leds`, `Output`, `Sink`. `Sink` takes non-owning refs to audio/UI. C BT callbacks use `Sink::active()`.

## Data path (v0.1)

```
Phone (A2DP / SBC)
  → Bluedroid decode → PCM
  → a2d_data_cb → Output::write (ringbuffer)
  → i2s_wr task (woken after prefetch)
  → I2S → PCM5102A → jack
```

Connection / audio events go: `a2d_cb` → work queue → `bt_work` → open/start/stop audio + LED status.

## Notes

- One I2S owner (`Output`); BT never drives the DAC directly.
- A2DP data callback only enqueues (non-blocking).
- Internal SBC decode (`BT_A2DP_USE_EXTERNAL_CODEC` off).
- Reference: IDF 6.1 `a2dp_sink_stream`.
