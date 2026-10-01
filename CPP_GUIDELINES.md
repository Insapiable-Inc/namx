# C++ guidelines

Coding preferences for the GUI code under `gui/`. The upstream library (`NAM/`,
`tools/`, `Dependencies/`) keeps its own style.

These rules apply to new and touched code. Existing code is brought in line
separately; a guideline landing here does not by itself mean a refactor.

**Readability comes first.** If following a rule would make a piece of code more
convoluted, stop and raise it instead of forcing it through. A wall of casts is a sign
of a design problem, not something to apply mechanically.

## Standard

**C++23** (decided 2026-10-01). C++26 was left out because no LTS distro or MSVC
supports it yet. Toolchain floor: GCC 14, Clang 18, MSVC 2022 17.10, Ubuntu 24.04.

Prefer the standard vocabulary types over hand-rolled ones:

| Instead of | Use |
|---|---|
| pointer + length parameter | `std::span<T>` / `std::span<const T>` |
| `const std::string&` for read-only text | `std::string_view` |
| `bool`, error code, out-parameter or sentinel value | `std::expected<T, Error>` (see Errors) |
| raw `for` over indices when a range algorithm says it | `std::ranges::…` |
| `M_PI`, hand-typed constants | `std::numbers::pi_v<float>` |
| `printf` / `std::cout` in tools and tests | `std::print` (GUI logging stays on `qCWarning`) |

Qt API boundaries keep Qt types (`QString`, `QList`): convert at the edge, not
everywhere.

## Naming

| Kind | Style | Example |
|---|---|---|
| Classes, structs, enums, type aliases | `PascalCase` | `NoiseGateProcessor`, `ParamDesc` |
| Functions, methods | `snake_case` | `set_threshold`, `take_gain_reduction_db` |
| Locals, parameters, data members | `snake_case` | `envelope`, `release_ms` |
| Constants (`constexpr`, `static const`, enumerators) | `SCREAMING_SNAKE_CASE` | `MAX_GR_DB` |

- **No prefixes or Hungarian-style markers**: no `m`, `k`, `s`, `g`, `p`. A member
  is `envelope`, not `mEnvelope`.
- **Unit suffixes** are ordinary words and follow the casing of the name they end:
  `release_ms`, `threshold_db`, `rate_hz`, `MAX_GR_DB`.
- **Library names stay as the library spells them.** Qt (and std, Eigen…) API is
  used as-is, and overrides keep the base-class name (`paintEvent`,
  `mousePressEvent`, `sizeHint`), and so do names a framework looks up by spelling
  (Qt Test's `initTestCase` / `cleanupTestCase`, `Q_PROPERTY` accessors). Everything we
  declare ourselves follows the table.

### Data members and accessors

- **Plain containers expose public members** and have no accessors. A container is a
  struct whose fields can take any combination of values: `ParamDesc`, a settings
  bundle, an effect's atomic knob set. Example: `gate.threshold_db.store(x)`.
- **Classes with an invariant keep members private** and go through `get_`/`set_`:
  `get_threshold_db()` / `set_threshold_db()`, member `threshold_db`. The setter is
  where the invariant is enforced.
- **No trailing-underscore members** (`threshold_db_`): it's too easy to miss and to
  mix up with the accessor.
- **No parameter or local reuses a member's name** (decided 2026-10-01). Without
  prefixes, a same-named local would silently hide the member. The conventions:
  - a setter or constructor parameter that feeds a member is `new_<member>`:
    `set_bpm(double new_bpm)`, `KnobWidget(const QString& new_label)`;
  - a per-block snapshot of an atomic is `current_<member>`:
    `const int current_mode = mode.load();`;
  - per-channel state arrays are `<name>_per_channel`, so the channel's local
    reference keeps the short name: `float& gain = gain_per_channel[ch];`;
  - otherwise, pick a more precise name for one of the two.

  `-Wshadow` is not in the permanent flags; build once with
  `-DCMAKE_CXX_FLAGS=-Wshadow` after a rename to check.

## `auto`

Use `auto` only when a reader can tell the type without looking anything up.

- **Casts to class types: use `auto`.** The cast already names the type, so writing
  it again is redundant.
  ```cpp
  auto* server = qobject_cast<QTcpServer*>(sender());
  ```
- **Built-in types (`int`, `float`, `bool`, `size_t`…): spell them out**, even when a
  cast names them. Range-for is the exception (see below), so
  `for (auto v : values)` over a `std::vector<int>` is fine.
  ```cpp
  int ms = static_cast<int>(seconds * 1000);   // not: auto ms = static_cast<int>(…)
  ```
- **Factories that name the type (`make_unique`, `make_shared`, `new`): either way.**
  `auto knob = std::make_unique<KnobWidget>(…)` and
  `std::unique_ptr<KnobWidget> knob = …` are both fine.
- **Iterators and lambdas: use `auto`.**
  ```cpp
  for (auto it = cache.begin(); it != cache.end();)
  auto on_change = [this](float v) { … };
  ```
- **Range-for and structured bindings: use `auto`.** The element type comes from the
  container.
  ```cpp
  for (const auto& [id, fx] : effect_table)
  for (auto& comb : combs_l) {
      comb.reset();
  }
  ```
- **Function results: spell the type.** If the type is only known by looking up the
  callee, write it out.
  ```cpp
  bool applied = apply_preset_params(doc.object());   // not: const auto applied = …
  QMenu* help_menu = menu_bar->addMenu("Help");       // not: auto* help_menu = …
  ```
- **Numbers: never use `auto`.** `auto gain = 0.5` is a `double`, and
  `auto n = frames / 2` is an `int`. Write `float gain = 0.5f`.

## Ownership

Every heap object has exactly one owner, and the code makes that owner obvious.

- **QObjects are owned by their Qt parent.** Create them with `new` and a parent, and
  hold them as plain pointers. Never put a QObject in a `std::unique_ptr` or
  `std::shared_ptr`: if a parent is ever passed too, both systems try to free it.
  `delete` / `deleteLater()` are only for removing a QObject early.
- **Factories for platform implementations** (`IAudioBackend::create()`) return a
  `std::unique_ptr` when the type is not a QObject. When it is a QObject
  (`MidiInput::create(parent)`), they take the parent and return a plain pointer.
- **Objects handed to the audio thread go through `RtSlot`.** Build the object on the
  GUI thread and `publish()` it as a `std::unique_ptr`. `RtSlot` frees the previous
  one once the audio thread can no longer be reading it. The raw owning
  `std::atomic<T*>` and its `delete` exist only inside `RtSlot`. Nobody else writes
  their own lock-free pointer swap.
- **References vs pointers for things you use but don't own.** Use a reference
  (`T&` / `const T&`) by default. Use a pointer (`T*`) only when the target can be
  null, gets re-pointed, sits in a container, or has to be atomic. A raw pointer
  never owns: ownership is a value member, a `std::unique_ptr` or a Qt parent.
  ```cpp
  void apply_theme(const Theme& theme);        // always present → reference
  Effect* selected = nullptr;                  // may be nothing → pointer
  std::vector<Effect*> chain;                  // container → pointer
  KnobWidget* knob = new KnobWidget(this);     // Qt-owned; we hold a view → pointer
  ```

## Inheritance

- **Inherit only to implement an interface, or because a framework requires it**
  (`QObject`, `QWidget`, `QAbstractItemModel`…).
- **Interfaces are pure.** Only `= 0` virtual functions and a virtual destructor:
  no data members, no implemented behavior. Example: the effect interface
  (`process`, `reset`, `params`…).
- **No implementation inheritance.** Behavior shared by several implementations goes
  into a member (e.g. an `EffectState` struct for the enabled/stereo/reset flags) or
  a free function (e.g. the mono/dual/stereo routing). Don't put it in a base class
  that subclasses half-override.
- **Platform code without a runtime choice is one class with per-platform `.cpp`
  files**, selected by CMake. Use a runtime interface only where a runtime choice
  exists or is planned. The audio backend stays an interface (`IAudioBackend`)
  because Windows will offer several (WASAPI shared/exclusive, possibly ASIO).
- **Instances of an interface are owned by `std::unique_ptr`**, so the number of
  instances stays open (e.g. two EQs in one chain). Instances the audio thread reads
  are created on the GUI thread and reach the audio thread through `RtSlot`.

## Errors

- **We don't throw.** Exceptions from libraries (the NAM core, `std::filesystem`,
  nlohmann JSON) are caught at the call site that wraps the library and turned into a
  return value. No exception crosses our own function boundaries.
- **Fallible operations return `std::expected<T, Error>`.** `Error` carries a
  human-readable message, so the UI can say *why* something failed ("model uses an
  unsupported architecture"), not just that it did. No `bool` results, `QString*
  error` out-parameters or sentinel values. `std::optional` is not used for errors.
- **Programmer errors are `assert`s**: things that can't happen if the code is right
  (wrong thread, index out of range). Checked in Debug, free in Release.
- **The audio thread never fails loudly.** It can't throw, allocate a message or log.
  It bypasses or outputs silence and sets an atomic flag that the GUI reads and
  reports. Everything that runs on the audio thread is `noexcept`.
- **Logging goes through Qt categories** (`qCWarning(lcAudio, …)`), never
  `std::cerr`: it is invisible on Windows.

## `const`, `constexpr`, `noexcept`

- **`const` by default.** Locals that never change after initialization are `const`,
  and so are member functions that don't modify the object. Reference parameters are
  `const T&` unless the function modifies the argument. Don't put `const` on
  by-value parameters (`void f(const int n)`): it's noise in the signature.
  ```cpp
  const float gain = db_to_linear(gain_db);
  float get_threshold_db() const;
  ```
- **`constexpr` for compile-time constants, tables and pure helpers.** Never
  `#define` a constant.
  ```cpp
  constexpr float MAX_GR_DB = 60.0f;
  constexpr float db_to_linear(float db) {
      …
  }
  ```
- **`noexcept`** goes on everything the audio thread calls, on move constructors and
  move assignment, and on swaps. Destructors are already implicitly `noexcept`.
  Leave it off elsewhere: a wrong `noexcept` turns an exception into
  `std::terminate`.

## Formatting

`gui/.clang-format` is the source of truth. Run `clang-format -i` on what you touch.
It overrides the upstream root `.clang-format`, which `NAM/` and `tools/` keep.

- **4-space indent**, no tabs. **100-column** limit.
- **Braces on their own line for namespaces and types** (`class`, `struct`, `union`,
  `enum`). Everywhere else (functions, control statements, lambdas) the brace is
  attached.
  ```cpp
  class NoiseGate
  {
  public:
      void process(float* buf, int frames) {
          if (frames == 0) {
              return;
          }
          …
      }
  };
  ```
- **Always use braces**, even for one-line `if` / `for` / `while` bodies.
- **No one-line functions**, not even trivial accessors. The body goes on its own
  line. Only empty lambdas (`[] {}`) stay on one line.
- **`*` and `&` go with the type**: `float* buf`, `const Theme& theme`.
- **No column alignment** of declarations, assignments, initializers or trailing
  comments.

## Headers and includes

- **`#pragma once`** in every header. No `#ifndef` guards.
- **Include order (Google style).** One block per group, separated by a blank line,
  sorted alphabetically inside each block. `gui/.clang-format` enforces this.
  1. The file's own header (`Foo.cpp` includes `"Foo.h"` first, which proves the
     header compiles on its own).
  2. The standard library: `<atomic>`, `<vector>`.
  3. Third-party libraries: Qt, JACK, NAM, Eigen, Windows headers.
  4. Project headers: `"EffectBase.h"`.
  ```cpp
  #include "NoiseGateProcessor.h"

  #include <algorithm>
  #include <atomic>

  #include <QWidget>

  #include "EffectBase.h"
  ```
- **Quotes for our headers, angle brackets for everything else**, including Qt
  (`<QWidget>`).
- **Include what you use.** Every file includes the headers for the names it uses
  directly, and doesn't rely on getting them through another header.
- **No forward declarations for build speed.** Include the header. Build times are
  the toolchain's problem, not the programmer's. A forward declaration is only for
  breaking a genuine circular dependency.
- **Headers whose order matters** (e.g. `<windows.h>` before other Win32 headers) are
  wrapped in `// clang-format off` / `// clang-format on`, with a comment saying why.

## Types and casts

- **`enum class` always**, never a plain `enum`. Give it an explicit underlying type
  when the value is stored or serialized: `enum class NoteValue : std::uint8_t`.
- **Integer types:**
  - **Sizes, counts and indices are `size_t`**, frame counts in our own APIs
    included (`process(float* buf, size_t frames)`). Indexing then needs no casts.
  - **`int` is for values that can be negative** (semitones, offsets, MIDI values) or
    that an external API dictates. Convert once at that boundary (Qt's `qsizetype`,
    JACK's `jack_nframes_t`), not at every use.
  - **Subtracting indices:** write it so it can't go below zero
    (`(write + size - delay) % size`), or keep it inside a small abstraction such as
    `DelayLine`, not spread through calling code.
  - Fixed-width types (`std::uint64_t`, `std::int16_t`) when the exact size matters:
    file formats, atomics shared across threads, MIDI bytes.
  - No implicit mixing of signed and unsigned: convert explicitly.
  - Spell them with `std::` and include `<cstdint>` / `<cstddef>`. The bare names
    (`uint64_t`) are the same types, but they're only guaranteed to exist through the
    C headers. `size_t` is the exception: it is in the `npl` using-list (see
    Namespaces), so it is written bare.
- **Casts:**
  - No C-style casts (`(int)x`). Use the named casts, so each one says what it does.
  - `qobject_cast` for QObject downcasts. `dynamic_cast` only outside Qt, and rarely.
  - `reinterpret_cast` only at C-API boundaries (JACK, WASAPI), with a comment.
  - `std::bit_cast` for reinterpreting bits.
- **Warnings:** `-Wall -Wextra -Wconversion -Wsign-conversion -Wsuggest-override`
  (`/W4` on MSVC). `-Werror` once the existing warnings are cleared.

## Namespaces

- **All of `gui/` lives in one flat namespace, `npl`**, with no nested namespaces.
  `npl` is a placeholder until the app gets its final name; the namespace is renamed
  with it. It can't be `nam`: the NAM core uses `nam::`.
- **Common std names are imported once, inside `npl`,** in a shared header, so our
  code writes `string` and `vector` instead of `std::string` and `std::vector`:
  ```cpp
  namespace npl
  {
  using std::array, std::atomic, std::expected, std::make_unique, std::size_t, std::span,
      std::string, std::string_view, std::unique_ptr, std::vector;
  }
  ```
  The list grows deliberately. Anything not on it keeps its `std::` prefix.
  `using namespace std;` is never allowed, anywhere.
- **Constants go in `npl`** or in the class they belong to (`NoiseGate::MAX_GR_DB`).
  No constants at global scope.
- **Helper functions local to a `.cpp` are `static`.** No anonymous namespaces.

## Comments

- **Comments explain *why*, not *what*:** intent, constraints, anything surprising.
  The code already says what it does.
  ```cpp
  // Tag read AFTER the swap: a block that saw `prev` either finished before
  // this read or will bump the count past it.
  ```
- **Plain `//` everywhere.** We don't generate docs, so no `///`, `/** */` or Doxygen
  tags (`@param`, `@return`). Editors show the comment above a declaration either way.
  Comment classes and public functions when the name and signature don't tell the
  whole story.
- **Always state units, ranges and threads** when the type doesn't: "dB, ≤ 0",
  "audio thread only", "called from the GUI thread".
- **No commented-out code.** Git keeps the history.
- **`TODO`s carry context:** `// TODO(phase-20): …` or a short reason. No bare
  `// TODO`.
- **No decorative banners** (`// ---- Helpers ----`). A file that needs sections
  probably wants to be split.

## Lambdas

- **One statement, like a Python lambda.** If a lambda needs more, the body becomes a
  named function and the lambda only forwards to it:
  ```cpp
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
      on_download_finished(reply);
  });
  ```
- **Connect straight to a member function** when the lambda would only forward with
  the same arguments: `connect(button, &QPushButton::clicked, this, &MainWindow::save);`.
- **Capture explicitly** (`[this, path]`), never `[=]` or `[&]`.
- **Stored lambdas capture by value.** That covers connects, callbacks, timers and
  network replies. Capture by reference only when the lambda runs right away
  (`std::ranges::sort(v, [&key](…) { … })`).
- **Qt connects pass a context object** (`this`, the third argument), so the
  connection dies with the receiver instead of calling into a deleted object.

## Audio thread

Code on the audio thread has a hard deadline every block. Missing it is an audible
dropout.

- **Nothing in `process` (or anything it calls) may:**
  - allocate or free memory (`new`, `vector::push_back`, building a `string`, a
    `std::function` that allocates);
  - take a lock (the only allowed pattern is `try_lock` and skipping the block, as the
    EQ does);
  - do I/O or logging (files, `qDebug`, `qCWarning`);
  - call Qt (`QString`, signals);
  - throw (everything there is `noexcept`).
- **GUI ↔ audio communication goes only through:** atomics for simple values,
  `RtSlot` for objects, and atomic flags or counters back to the GUI (meters, tuner,
  error flags).
- **Buffers are sized before audio runs** (prepare, sample-rate or buffer-size
  change), never on demand.
- **Denormals are flushed** (FTZ/DAZ at the top of the callback, done by the
  backends).
- **Every new or changed DSP stage is measured with `NAMPlayerBench`** against the
  block budget before it lands.
- **Functions that run on the audio thread say so** with an `// audio thread`
  comment.

## Tests

- **Deterministic, always. No fuzzing**, no random inputs, no randomized or
  property-based tests. The same build gives the same result every run. Inputs are
  fixed and written in the test.
- **Tests never touch anything outside their own temporary directory.**
  - Files go in a `QTemporaryDir` the test creates.
  - Settings and app data use `QStandardPaths::setTestModeEnabled(true)`.
  - No test deletes, moves or overwrites a path it didn't create, and none of them
    builds such a path from an environment variable, the home directory or user
    input.
- **Few tests, each one justified.** Every test protects something that has broken or
  would be expensive to break: a fixed bug, a file format, a thread-safety mechanism.
  No tests written for coverage numbers.
- **What gets tested:** logic without UI.
  - Preset round trip, and old files still loading.
  - The chain-order sanitizer.
  - `RtSlot`.
  - `MidiMapper`.
  - `Tone3000Client` against the local mock.
  - Targeted DSP checks: bypass is bit-exact, `reset()` clears state, no NaN on fixed
    extreme settings.

  Widgets are covered by the roadmap's manual verification lists and
  `--self-test-and-exit`.
- **Headless:** no audio device and no internet. Only the local mock server.
- **Qt Test**, one `tests/<Class>Test.cpp` per class under test, all registered with
  `ctest` and run in CI on every push.

## Syntax

- **Initialization:**
  - `=` for simple values: `int count = 0;`, `string name = "Drive";`.
  - Braces for aggregates and containers, with no space before the brace:
    `vector<int> v{1, 2, 3};`, `ParamSpec spec{…};`.
  - Default member values go in the class (`float gain = 1.0f;`). Constructors don't
    repeat them.
- **`using`, never `typedef`:** `using SampleBuffer = vector<float>;`.
- **`const` on the left:** `const T&`, not `T const&`.
- **`nullptr`, never `NULL` or `0`** for pointers.
- **No trailing return types** (`auto f() -> int`) unless a template needs one.
- **Every override says `override`, and doesn't repeat `virtual`:**
  `void reset() override;`. `-Wsuggest-override` catches a missing one.
- **`= default` / `= delete`** instead of empty bodies or private-and-undefined
  members.
- **Range-`for` when the index isn't needed.** DSP loops over `nframes` keep their
  index.
- **Early returns:** guard clauses at the top of a function instead of nested `if`s.
  ```cpp
  void load(string_view path) {
      if (path.empty()) {
          return;
      }
      …
  }
  ```
- **One declaration per line.** `float a, b;` becomes two lines; structured bindings
  (`auto [id, fx]`) are the exception.
