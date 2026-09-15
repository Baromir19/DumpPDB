# Architecture

## Repository layout

```
DumpPDB/
├── API/            PdbAPI.dll — C ABI wrapper around PdbToolset
├── CLI/            DumpPDB.exe — command-line front-end
├── Core/           Static library shared by API and CLI
│   ├── Binary/     Generic binary file view (BinaryFile, BinaryView, BinarySection)
│   ├── Config/     INI persistence (IniFile, IniSerializer, SaveManager, SettingsRegistry)
│   ├── DIA/        DIA SDK wrappers (DiaSession, TypeWalker, TypeBuilder, SymbolFinder, SymbolDumper)
│   ├── Dump/       Type rendering subsystem (see below)
│   ├── PE/         Portable Executable parser (PEImage, PESection)
│   ├── Search/     Binary scan (BinaryScanner, StringScanner, SignatureScanner, RegexFilter)
│   ├── System/     ConsoleManager, ClipboardManager
│   └── Util/       ComPtr, Singleton, DumpError, HashUtils, StringUtils, DebugManager
├── TestCompiland/  Compiled test binary used as the source of PDB symbols for integration tests
├── Tests/          GoogleTest suites (Unit + Integration)
└── scripts/        Python tooling (dump_pdb.py, reconstruct_sources.py, etc.)
```

---

## Core data flow

```
PDB file
   │
   ▼
DiaSession          ← COM/DIA session lifecycle (CoInitialize, loadDataFromPdb, openSession)
   │
   ▼
PdbToolset          ← Singleton facade; owns DiaSession + SymbolDumper
   │    │
   │    └─ SymbolFinder  ← findFirst / findAll / findByNamespacePrefix
   │
   ▼
SymbolDumper        ← produces std::wstring C++ declarations from IDiaSymbol*
   │
   ├── DumpContext       shared mutable state (config, scope stack, session ptr, template info)
   ├── TypeWalker        resolveType() — DIA tree → TypeBuilder chain
   ├── TypeBuilder       spiral/right-left rule renderer for C++ declarators
   │
   └── Renderers (all talk to SymbolDumper via IDumpCoordinator):
       ├── ClassRenderer       class / struct / union
       ├── EnumRenderer        enum / enum class
       ├── TypedefRenderer     typedef
       ├── FunctionRenderer    member functions, virtuals, friends
       ├── MemberLayout        data members, bit-fields, nested anonymous UDTs
       ├── TopLevelDispatcher  namespace wrapping, routes to correct renderer
       ├── DumpFormatter       indentation, access labels, curly-brace style
       ├── ConstantRenderer    enum values and integer constants
       ├── TypeSourceTracker   source-file lookup via DIA line info
       └── ScopeTracker        pushScope / popScope for nested-type name stripping
```

---

## PdbToolset singleton

`PdbToolset` is a `Singleton<PdbToolset>` defined in `Core/PdbToolset.hpp`. It solves the original lifetime problem where `DiaSession` was a local variable that got destroyed before the output was used.

All public entry points (`dumpTypeByName`, `dumpClassByName`, `enumerateSymbolNames`, `findStringsInFile`, etc.) live on `PdbToolset`. Both the CLI (`CommandType`, `CommandCompiland`, etc.) and the C API (`API/PdbApi.cpp`) call through `PdbToolset::instance()`.

---

## Dump subsystem

The renderers are mutually recursive: a class can contain member functions, member functions reference types, types can be nested classes, and so on. To break the header-include cycles the renderers do not include each other — they all communicate through `IDumpCoordinator`, which `SymbolDumper` implements privately. `SymbolDumper` wires everything together in its constructor.

`DumpContext` is the single shared state object. Every renderer holds a `const DumpContext&` (or `DumpContext&` where mutation is needed) so any configuration or session change in `SymbolDumper::setConfig()` propagates automatically.

---

## C API (PdbAPI.dll)

`API/PdbApi.h` exposes a flat `extern "C"` surface. All calls are serialized by a single `std::mutex`. The buffer convention (size-query mode vs copy mode) is the same for every `Dump*` / `GetLastError` function:

- Pass `nullptr` / `0` as the buffer → writes required `wchar_t` count (incl. null) to `*a_outRequiredSize`.
- Pass a real buffer → copies if it fits; returns `PDBAPI_ERROR_BUFFER_TOO_SMALL` otherwise.

See [api.md](api.md) for the full reference.

---

## CLI front-end

```
wmain
  └── Application::initialize
        ├── SaveManager::initialize   ← loads config.ini
        ├── ConsoleManager::initialize ← parses argv
        ├── CommandManager::initialize ← registers commands
        ├── PdbToolset::initialize     ← opens PDB (if command needs it)
        └── CommandManager::executeCommand
```

Each command is a `ICommand` subclass. The type mask in `ICommand::Type` determines whether the PDB needs to be opened before execution (`s_executableMask = 0xF0`).

---

## Binary search

`BinaryScanner` (in `Core/Search/`) loads a file through `PEImage` when it is a valid PE, scanning only string-candidate sections (`.text`, `.rdata`, `.data`, etc.). For non-PE files the whole file is scanned. The two search modes are:

- **String search** (`StringScanner`) — finds null-terminated ASCII, UTF-8, or UTF-16LE strings above a minimum length, with optional regex filtering.
- **Signature search** (`SignatureScanner`) — wildcard byte patterns in formats `"FF ?? 01 BD"`, `"FF??01BD"`, `"0xFF??01BD"`, `"{ FF ?? 01 BD }"`.

---

## Testing

| Suite | Location | What it covers |
|---|---|---|
| Unit | `Tests/Unit/` | `TypeWalker`, `TypeBuilder`, `ScopeContext` |
| Integration | `Tests/Integration/` | Full PDB round-trips via `TestCompiland.pdb` |

`TestCompiland/` is a small C++ project compiled specifically to produce a known PDB. Integration tests open that PDB and assert on the dumped output.
