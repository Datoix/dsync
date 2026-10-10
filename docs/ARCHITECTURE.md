# dsync architecture

## Layout

```
main/                     app_main — compose owners
components/
  board/                  Kconfig pin getters
  idf_handles/            unique_ptr wrappers for IDF/FreeRTOS C handles
  audio_out/              I2S TX + ringbuffer + i2s_wr task
  bt_sink/                Classic A2DP sink (bt_sink / bt_controller / bt_a2dp)
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
- A2DP data callback only enqueues: no blocking and no logging. Throughput stats
  are emitted by the `i2s_wr` task.
- `Output` allocates its I2S channel / ring / writer task once and keeps them for
  the process lifetime; shared state is `std::atomic`, so teardown can never race
  the data callback.
- Ring size and playback cushion are Kconfig knobs under *dsync audio*.
- Internal SBC decode (`BT_A2DP_USE_EXTERNAL_CODEC` off).
- Reference: IDF 6.1 `a2dp_sink_stream`.
