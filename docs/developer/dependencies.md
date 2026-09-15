# Build Dependencies

## Toolchain requirements

| Requirement | Version |
|---|---|
| MSVC | 2019 or later (C++17, `/Zi`, `__declspec`) |
| CMake | 3.20 or later |
| Windows SDK | Any recent version |

The project is Windows-only. It uses Win32 APIs (`CoInitializeEx`, clipboard, console cursor positioning) and MSVC-specific intrinsics (`__declspec(dllexport/dllimport)`). Clang-cl is supported for tidy runs.

---

## DIA SDK

The Debug Interface Access (DIA) SDK is the only external C++ dependency. It ships as part of Visual Studio.

CMake locates it automatically in this order:

1. `%VSINSTALLDIR%\DIA SDK` — set by `vcvarsall.bat` or the `ilammy/msvc-dev-cmd` GitHub Actions step.
2. `vswhere -latest` — falls back to the latest VS installation found on the machine.

Required files:

| File | Role |
|---|---|
| `DIA SDK/include/dia2.h` | DIA type declarations |
| `DIA SDK/lib/amd64/diaguids.lib` | CLSID/IID linker input (x64) |
| `DIA SDK/lib/diaguids.lib` | CLSID/IID linker input (x86) |
| `DIA SDK/bin/amd64/msdia140.dll` | Runtime DIA COM server (x64) |
| `DIA SDK/bin/msdia140.dll` | Runtime DIA COM server (x86) |

`msdia140.dll` is copied to the output directory automatically by the `CopyDiaDll` CMake target.

---

## GoogleTest (optional)

Tests are built only when `-DBUILD_TESTS=ON` is passed to CMake. GoogleTest is fetched via `FetchContent` at configure time — no manual installation is needed.

```
cmake -DBUILD_TESTS=ON ...
```

---

## Python tooling (optional)

The `scripts/` directory requires:

| Package | Minimum version | Purpose |
|---|---|---|
| Python | 3.10 | Script runtime |
| tkinter | stdlib | `verify_sources.py` GUI |

No additional `pip` packages are required. All logic is in the bundled `dumppdb_tools` package.

---

## Configuring and building

```bat
:: x64 with Visual Studio generator
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

:: x64 Ninja (CI-style, requires vcvarsall.bat or msvc-dev-cmd)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

:: Include tests
cmake -B build -G Ninja -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Outputs land in `build/bin/<Config>/`:
- `DumpPDB.exe`
- `PdbAPI.dll` + `PdbAPI.lib`
- `TestCompiland.exe` (test PDB source)
- `msdia140.dll` (copied automatically)
