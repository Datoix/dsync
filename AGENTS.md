# dsync — agent guide

ESP32 Classic Bluetooth A2DP sink → PCM5102A → headphone jack. Needs an **ESP32 with
Classic BT** (not ESP32‑S3). Toolchain: **ESP-IDF 6.1**.

## Layout

```
main/                 app_main — compose owners
components/
  board/              Kconfig pin getters
  idf_handles/        unique_ptr wrappers for IDF/FreeRTOS C handles
  audio_out/          I2S TX + ringbuffer + i2s_wr task
  bt_sink/            Classic A2DP sink (bt_sink / bt_controller / bt_a2dp)
  ui_leds/            status LED (atomic status + periodic timer)
docs/                 ARCHITECTURE, HARDWARE, ROADMAP, VERSIONS
```

Namespaces: `dsync::board`, `dsync::handles`, `dsync::audio`, `dsync::bt`, `dsync::ui`.
`app_main` owns `Leds`, `Output`, `Sink`. `Sink` takes non-owning refs to audio/UI.

## Build & flash

```bash
. $HOME/.espressif/tools/activate_idf_v6.1.sh
idf.py set-target esp32      # once per clean tree
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor   # Ctrl+] to quit
```

Menuconfig: *dsync board* (GPIOs) and *dsync Bluetooth* (device name, SSP).

## Guardrails

- **Validate before acting.** Before doing what the user asks (or proposing a change),
  briefly check whether it is a good fit for this ESP-IDF codebase. If it looks doubtful,
  suboptimal, or cargo-cult (e.g. a nested `CMakeLists.txt` that is not a real IDF
  component, Arduino APIs in an IDF project, blocking work in an ISR) — do not implement
  it yet. Say why it may be a bad idea, suggest the better default if you have one, and ask
  them to confirm. Proceed only after explicit confirmation, or when the request is clearly
  sound.
- **Ask before packages / sudo.** Do not run without explicit user OK: `sudo`, system
  package managers, or installs that change the env/lockfiles (`pip` / `uv` / `npm` /
  `idf.py add-dependency` / `cargo install`, …) — unless the user already asked for that
  exact action. Stop and ask instead. Building with an already-configured ESP-IDF
  (`idf.py build` / `flash`) is fine without asking.

## C++ / ESP-IDF conventions

Full style and ESP-IDF conventions live in the [`esp-idf-cpp` skill](.agents/skills/esp-idf-cpp/SKILL.md);
they apply to all `**/*.{c,cc,cpp,h,hpp,inc}` in this repo. Shared C++ style is in
[`references/cpp-style.md`](.agents/skills/esp-idf-cpp/references/cpp-style.md).

## Skills

- `esp-idf-cpp` — C++ / ESP-IDF conventions for this repo.
- `suggest-commit-message` — propose a scoped commit message for pending changes.

Discovered automatically from `.agents/skills/` by Cursor and Cline, and from
`.claude/skills/` by Claude Code. See `docs/ARCHITECTURE.md` for the data path.
