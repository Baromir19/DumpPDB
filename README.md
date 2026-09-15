# DumpPDB

A PDB reader that reconstructs C++ type declarations with field offsets, sizes, access specifiers, virtual functions, and source file information.

---

## Quick start

```
DumpPDB.exe -type <typename> <file.pdb>
DumpPDB.exe -type "TemplateType<Arg>" file.pdb   # quote names with < >
```

**Example output:**

```cpp
// size: 52 byte
class MainObjectDefinition : public ExposedDefinition
{
    /// TYPEDEFS:
    typedef ClassBeingDefined MainObject;

    /// VIRTUALS:
    virtual MainObject* CreateObject(); // 0x0
    virtual void Init();                // 0x0
    virtual void ~MainObjectDefinition(); // 0x0

    /// FUNCS:
    void MainObjectDefinition();
    MainObjectDefinition& operator=();
};
// f:\user\sample\mainobject.cpp
// f:\user\sample\maininstance.cpp
```

---

## Commands

| Command | Description |
|---|---|
| `-type <name>` | Dump a type by name (class, struct, union, enum, typedef). Copies to clipboard. |
| `-compilands [true]` | List compilation units. Pass `true` for compiler/env details. |
| `-sources` | List all source files recorded in the PDB. |
| `-settings [-h] <key> <value>` | Read or write a persistent setting. `-h` lists all. |
| `-help` | Print the command table. |

The search is case-insensitive. Template names must be quoted in the shell (`"TList<int>"`). If no exact match is found, a namespace-prefix fallback is applied automatically.

---

## Settings

Output is controlled by `config.ini` next to the executable (created on first run). Key settings:

| Key | Default | Description |
|---|---|---|
| `DumpConfig.ShowOffset` | `true` | Field byte offsets. |
| `DumpConfig.ShowAccess` | `true` | Access specifier labels. |
| `DumpConfig.ShowTypeSource` | `false` | Source file paths below the type body. |
| `DumpConfig.HideCompilerGenerated` | `true` | Hide compiler-generated helpers. |
| `DumpConfig.IntStyle` | `1` | `0` = `__int32`, `1` = `int32_t`. |
| `DumpConfig.TemplateParams` | `false` | Emit `template<...>` for instantiation queries. |
| `CommandConfig.UseClipboard` | `true` | Auto-copy `-type` output to clipboard. |

Full reference: [`docs/user/settings.md`](docs/user/settings.md)

---

## C API (PdbAPI.dll)

The core logic is also exposed as a flat `extern "C"` DLL, suitable for use from Python, C#, or any language with a foreign function interface.

```c
PdbApi_Initialize(L"Dev.pdb");

uint32_t size = 0;
PdbApi_DumpTypeByName(L"Actor", 0, nullptr, 0, &size);

wchar_t* buf = malloc(size * sizeof(wchar_t));
PdbApi_DumpTypeByName(L"Actor", 0, buf, size, nullptr);
```

Other functions: `PdbApi_EnumerateSymbolNames`, `PdbApi_GetSymbolSourceFiles`, `PdbApi_FindStringsInFile`, `PdbApi_FindSignaturesInFile`.

Full reference: [`docs/developer/api.md`](docs/developer/api.md)

---

## Python scripts

`scripts/` contains tooling built on top of `PdbAPI.dll`:

| Script | Purpose |
|---|---|
| `dump_pdb.py` | General-purpose query tool (types, symbols, strings, signatures). |
| `resolve_type_sources.py` | Map every PDB type to its best-matching source file. |
| `reconstruct_sources.py` | Render per-type source snippets from a template. |
| `recover_file_sources.py` | Recover the original source tree from PDB paths + binary strings. |
| `verify_sources.py` | GUI for reviewing and correcting type → source mappings. |

Full reference: [`docs/user/scripts.md`](docs/user/scripts.md)

---

## Building

Requires MSVC 2019+, CMake 3.20+, and the DIA SDK (ships with Visual Studio). CMake locates the DIA SDK automatically via `%VSINSTALLDIR%` or `vswhere`.

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

With tests:

```bat
cmake -B build -DBUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Details: [`docs/developer/dependencies.md`](docs/developer/dependencies.md)

---

## Documentation

| | |
|---|---|
| [CLI usage](docs/user/cli.md) | Commands, output format, template names |
| [Settings](docs/user/settings.md) | All `config.ini` keys |
| [Python scripts](docs/user/scripts.md) | Script reference |
| [Architecture](docs/developer/architecture.md) | Code structure, data flow |
| [C API](docs/developer/api.md) | PdbAPI.dll function reference |
| [Dependencies](docs/developer/dependencies.md) | Build setup |

---

## Roadmap

- Expanded function attribute output (`__declspec(__naked)`, `__noinline`, etc.)
- Compact visibility blocks (group successive same-access members under a single label)
- Compiler-hint annotations on generated functions (default constructors, copy operators)
- `void` cleanup for destructors (`void ~Object()` → `~Object()`)
- Namespace data output
- Code cleanup
