# C++ style (shared)

**Simple first:** KISS, DRY, SRP. Short single-purpose functions—no huge blocks. Prefer modern C++
that clarifies ownership and types; avoid cleverness.

- One module / type / function → one job. Split when a unit mixes I/O, parsing, and policy.
- Prefer types that own state (`struct` with methods) for behavior that needs that state. Free
  helpers are fine for pure utilities and one-off wiring.
- Private / leading-underscore members when there are invariants. Prefer clear names over
  abbreviations.
- No magic numbers for fixed config—named constants at module / board / `Kconfig` level.
- DRY: extract shared builders and helpers; don't copy-paste handler bodies.
- Format nested calls and long argument lists with one argument per line when it helps readability.
- Match nearby naming and layout in the package you touch; don't invent a parallel style.
