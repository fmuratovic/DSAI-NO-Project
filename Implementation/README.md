# Implementation

## Layout

| Target | Contents |
|---|---|
| `core` (static library) | `Network`, `Generator`, `OPFSolver` (`solve`, `solveWithLosses`), `DispatchSolver`, `Sweep`, `TestNetwork` |
| `cli` | validation suite (56 checks) and CSV export of the five deliverable figures |
| `dispatchGui` | `gui/main.cpp`: header strip + tabs Overview / Parameters / Network / Charts |

`make_figures.py` renders the CSV files written by `cli` into PNG figures (`pip install matplotlib pandas`).

## Method in one paragraph

The KKT conditions of the quadratic-cost DC-OPF are assembled into one sparse linear system per active-set iteration, with unknowns `[P | θ | λ | μ]`. Starting from an empty active set, the system is solved, every inequality is checked, and the single worst violation (generator bound or line limit) is added before re-solving. The matrix is indefinite with structural zeros on the diagonal, so it is factorised with `sparse::createDblSolver(NonSymmetric, LU, DiagonalMultiPass)` and every solution is residual-checked. Losses are added by an outer fixed-point iteration that re-injects half of each line's `g·Δθ²` at its end buses.

## Prerequisites

- CMake 3.18 or newer and a C++20 compiler (Visual Studio 2022, Apple Clang, GCC 12+);
- the natID SDK with its `mainUtils`, `natGUI` and `Matrix` libraries;
- the SDK location is taken from `NATID_SDK_ROOT` (default `%USERPROFILE%\natID.SDK` / `~/natID.SDK`).

## Build

```bat
cmake -S Implementation -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

On macOS/Linux replace the generator with `-G Ninja` or omit it. The cache variable
`GUI_STAGE` selects the GUI source: `app` (default, `gui/main.cpp`), `1`–`4` for the
development stages, `0` to build only `core` and `cli`.

## Running

- `cli` prints the validation suite (expected: 56 checks, 0 failures) and writes `fig1..fig5*.csv`;
- `dispatchGui` opens on the Overview tab; choose a scenario or edit parameters and press Solve.
  Export PDF is available on the Overview and Charts tabs.

## Portability notes

- `gui/WinMain.h` redirects the Windows entry point to `main()`; the include is a no-op elsewhere.
- `dispatch.cmake` derives the Matrix library path from the framework's `mainUtils` path, so
  `.lib` / `.dylib` / `.so` naming follows each platform automatically.
