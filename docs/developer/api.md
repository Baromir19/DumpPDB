# Public C API — PdbAPI.dll

Header: `API/PdbApi.h`  
Import library: `PdbAPI.lib`

All functions are `extern "C"` and use `__declspec(dllimport/dllexport)` via `PDBAPI_API`. The DLL is single-threaded internally (a global mutex serializes all calls).

---

## Buffer convention

Applies to every `Dump*`, `Enumerate*`, `GetSymbol*`, `Find*`, and `GetLastError` function:

| Condition | Behaviour |
|---|---|
| `a_outBuffer == nullptr` or `a_bufferSize == 0` | Size-query mode. Writes required `wchar_t` count (including null terminator) to `*a_outRequiredSize`. Returns `PDBAPI_OK`. |
| `a_outBuffer != nullptr` and buffer is large enough | Copies the result. Returns `PDBAPI_OK`. |
| `a_outBuffer != nullptr` but buffer is too small | Returns `PDBAPI_ERROR_BUFFER_TOO_SMALL`. Still writes the actual required size to `*a_outRequiredSize` so the caller can reallocate and retry. |

`a_outRequiredSize` may be `nullptr` if the size is not needed.

**Typical two-phase pattern:**

```c
uint32_t needed = 0;
PdbApi_DumpTypeByName(L"Actor", 0, nullptr, 0, &needed);

wchar_t* buf = malloc(needed * sizeof(wchar_t));
PdbApi_DumpTypeByName(L"Actor", 0, buf, needed, nullptr);
```

---

## Result codes

```c
PDBAPI_OK                   =  0
PDBAPI_ERROR_NOT_INITIALIZED = -1   // PdbApi_Initialize was not called
PDBAPI_ERROR_INVALID_ARG    = -2   // null pointer or invalid value
PDBAPI_ERROR_PDB_LOAD_FAILED = -3  // DIA could not open the PDB
PDBAPI_ERROR_NOT_FOUND      = -4   // symbol / string / signature not found
PDBAPI_ERROR_BUFFER_TOO_SMALL = -5 // buffer too small (see convention above)
PDBAPI_ERROR_EXCEPTION      = -6   // C++ exception caught; details in GetLastError
```

Call `PdbApi_GetLastError` after any non-OK result to retrieve a description.

---

## Lifecycle

```c
PdbApiResult PdbApi_Initialize(const wchar_t* a_pdbPath);
void         PdbApi_Shutdown();
int32_t      PdbApi_IsInitialized(); // 1 = initialized, 0 = not
```

`PdbApi_Initialize` opens a DIA session for the given PDB file. Must be called before any Dump or Enumerate function. `PdbApi_Shutdown` clears the error state but does not unload the DLL; call once when done.

---

## Configuration

```c
PdbApiResult PdbApi_SetConfig(const PdbApiDumpConfig* a_config);
PdbApiResult PdbApi_GetConfig(PdbApiDumpConfig*       a_outConfig);
```

`PdbApiDumpConfig` is a POD struct that mirrors `DumpConfig`. All fields are `int32_t` (bool-as-int) or `uint32_t` for ABI safety across compiler boundaries.

```c
struct PdbApiDumpConfig {
    int32_t  showSize;
    int32_t  showOffset;
    int32_t  showAccess;
    int32_t  showInfoComment;
    int32_t  showNonScoped;
    int32_t  showEnumHex;
    int32_t  showTypeSource;
    int32_t  curlyBraceNewline;
    int32_t  hideCompilerGenerated;
    int32_t  templateParams;    // 1 = emit template<...> for instantiations
    uint32_t baseAccessType;    // 0 = use symbol access, 1/2/3 = force private/protected/public
    int32_t  intStyle;          // 0 = MsvcNative (__int32), 1 = Cstdint (int32_t)
};
```

See [settings.md](../user/settings.md) for field descriptions.

---

## Dump functions

All take a name, a case-sensitivity flag, and the standard output buffer parameters.

```c
PdbApiResult PdbApi_DumpTypeByName(
    const wchar_t* a_name, int32_t a_caseSensitive,
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);

PdbApiResult PdbApi_DumpClassByName(
    const wchar_t* a_name, int32_t a_caseSensitive,
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);

PdbApiResult PdbApi_DumpEnumByName(
    const wchar_t* a_name, int32_t a_caseSensitive,
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);

PdbApiResult PdbApi_DumpTypedefByName(
    const wchar_t* a_name, int32_t a_caseSensitive,
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);
```

`DumpTypeByName` searches all symbol tags (UDT, enum, typedef) and falls back to a namespace-prefix search if no exact match is found. Symbols in the same namespace are grouped into one `namespace X { ... }` block. The specific `DumpClass/Enum/Typedef` variants return only the first match of that tag.

---

## Enumeration functions

```c
// Enumerate names of all UDT/enum/typedef symbols, newline-separated.
// a_topLevelOnly: 1 = global/namespace scope only, 0 = all including nested.
PdbApiResult PdbApi_EnumerateSymbolNames(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize,
    int32_t a_topLevelOnly);

// Enumerate fully-qualified names of a type's nested UDT/enum/typedef children.
PdbApiResult PdbApi_EnumerateNestedTypeNames(
    const wchar_t* a_name, int32_t a_caseSensitive,
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);

// Get source file paths for a named type, newline-separated.
PdbApiResult PdbApi_GetSymbolSourceFiles(
    const wchar_t* a_name, int32_t a_caseSensitive,
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);

// Enumerate all source file paths recorded in the PDB, newline-separated.
PdbApiResult PdbApi_EnumerateSourceFiles(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);
```

---

## Binary file search

### String search

```c
enum PdbApiStringEncoding : int32_t {
    PDBAPI_ENC_ASCII   = 1 << 0,
    PDBAPI_ENC_UTF8    = 1 << 1,
    PDBAPI_ENC_UTF16LE = 1 << 2,
};

PdbApiResult PdbApi_FindStringsInFile(
    const wchar_t* a_filePath,
    uint32_t       a_minLength,
    int32_t        a_encodingFlags,   // bitwise OR of PdbApiStringEncoding
    uint32_t       a_outStringFlags,  // bitwise OR of PDBAPI_STRING_SHOW_OFFSET | PDBAPI_STRING_SHOW_ENCODING
    const char*    a_sectionNames,    // comma-separated PE section names, "" = all
    const char*    a_regexPattern,    // optional regex filter, "" = no filter
    wchar_t*       a_outBuffer,
    uint32_t       a_bufferSize,
    uint32_t*      a_outRequiredSize);
```

Output format: one entry per line, optionally prefixed with `0x<offset>: ` and `[ENCODING] ` depending on `a_outStringFlags`.

For PE files, only string-candidate sections are scanned. For non-PE files, the whole file is scanned.

### Signature search

```c
PdbApiResult PdbApi_FindSignaturesInFile(
    const wchar_t* a_filePath,
    const char*    a_pattern,         // "FF ?? 01 BD", "FF??01BD", "{ FF ?? 01 BD }", etc.
    const char*    a_sectionNames,    // comma-separated PE sections, "" = all
    wchar_t*       a_outBuffer,
    uint32_t       a_bufferSize,
    uint32_t*      a_outRequiredSize);
```

Output: newline-separated hex offsets (`0x00012345`).

---

## Diagnostics

```c
PdbApiResult PdbApi_GetLastError(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);
```

Returns the most recent error message. The buffer convention applies. The message is cleared on a successful `PdbApi_Initialize` call.

---

## Python wrapper

`scripts/dumppdb_tools/api/pdb_api.py` wraps the entire C API via `ctypes`. Use `PdbClient` from `dumppdb_tools` for a higher-level interface.

```python
from dumppdb_tools import PdbClient

pdb = PdbClient("./PdbAPI.dll")
pdb.open("Dev.pdb")
print(pdb.dump_type("Actor"))
pdb.close()
```
