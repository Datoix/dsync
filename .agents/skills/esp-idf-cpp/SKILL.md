---
name: esp-idf-cpp
description: C++ and ESP-IDF conventions for this project — include guards, struct over class, std::unique_ptr for owning IDF/FreeRTOS handles, esp_err_t error handling, std::array/span/string over C types, non-owning lifetime rules, and ISR safety. Use when writing, reviewing, or refactoring any .c, .cc, .cpp, .h, .hpp, or .inc file in dsync.
---

# ESP-IDF C++ conventions

Use the project's documented IDF activation (`. $HOME/.espressif/tools/activate_idf_v6.1.sh`);
never invent a toolchain path.

Shared, language-level style lives in [references/cpp-style.md](references/cpp-style.md) — read it
when the task is about general C++ structure, not just IDF specifics.

## Rules

- Include guards (`#ifndef` / `#define` / `#endif`), not `#pragma once`.
- Prefer brace / designated struct initializers. For fat ESP C structs, init all fields (or an
  IDF `*_DEFAULT()` macro) so `-Wmissing-field-initializers` stays clean.
- Prefer `struct` over `class`.
- **Do not store owning raw pointers.** Owning heap / C handles use `std::unique_ptr` with a
  custom releaser (e.g. `esp_lcd` panel, `i2s_chan`, `FILE*`).

```cpp
struct I2sChanDeleter {
    void operator()(i2s_chan_handle_t h) const {
        if (h) { (void)i2s_del_channel(h); }
    }
};
using I2sChanPtr = std::unique_ptr<std::remove_pointer_t<i2s_chan_handle_t>, I2sChanDeleter>;
```

- Non-owning access: `T&` only if the referent cannot be destroyed out from under you (parent owns
  child). If lifetime is independent (async callbacks, other tasks) — use a weak handle, an index
  into the owner, or explicit unregister. Never split "check valid" and "use" across a race without
  a shared lock.
- Prefer `std::array` over C arrays; `std::string` / `string_view` for text; `std::span` for
  non-owning views; `std::function` / lambdas over `void*` callbacks. C arrays only when required
  (C APIs, packed layouts, log `TAG`s).
- K&R braces. Typical layout: `main/include/` + `main/src/` (and `components/<name>/`). `app_main`
  wires composition. File-local symbols in `namespace { }`.
- Errors: `esp_err_t` + `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`; one `TAG` per module
  (`ESP_LOGI(TAG, …)`).
- Keep ISR / timer callbacks short; don't block, allocate, or call heavy IDF APIs from them — defer
  to a task/queue.
