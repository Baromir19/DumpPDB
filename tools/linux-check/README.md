# Linux checks (no Windows, no DIA SDK, no Zydis)

These files let the parts of DumpPDB that do not really need Windows be built and exercised on
Linux with g++. They are **verification aids, not part of the product build**.

| Directory | Purpose |
|---|---|
| `probes/` | Small programs that run the executable analysis against a real `.exe`: `rtti_probe`, `an_probe` (statistics), `export_probe` (writes all reports), `insp_probe` (info / class / address). |
| `stubs/` | A stub `dia2.h` / `Windows.h` generated from the DIA calls found in the sources, plus a mock `TypeReconstructor`. Only good for `-fsyntax-only`: the DIA methods are templates, so **their names and signatures are not verified** against the real header. |
| `mock/` | A stub `dia2.h` with real virtual methods and `mock_test.cpp`, an in-memory fake PDB (namespace functions, globals, file statics, static members, local statics, publics) that drives `GlobalDumper` / `GlobalRenderer` and prints the real output. |
| `shim/gtest/gtest.h` | A tiny stand-in for GoogleTest so `tests/Unit/*.cpp` run without it. |

```bash
ROOT=$(pwd)                               # repository root

# executable analysis on a real exe
g++ -std=c++17 -O2 -I$ROOT tools/linux-check/probes/export_probe.cpp -o export_probe
./export_probe Game.exe analysis_dir

# unit tests (no gtest needed)
g++ -std=c++17 -DGT_SHIM_MAIN -Itools/linux-check/shim -I$ROOT tests/Unit/MsvcDemanglerTests.cpp -o t1 && ./t1
g++ -std=c++17 -DGT_SHIM_MAIN -Itools/linux-check/shim -I$ROOT tests/Unit/RttiImageTests.cpp   -o t2 && ./t2

# PDB layer: syntax check against the stubs, and run against the mock PDB
g++ -std=c++17 -fsyntax-only '-D__declspec(x)=' -Itools/linux-check/stubs -I$ROOT tools/linux-check/probes/stubcheck.cpp
g++ -std=c++17 -Itools/linux-check/mock -I$ROOT '-D__declspec(x)=' tools/linux-check/mock/mock_test.cpp -o mock_test && ./mock_test
```
