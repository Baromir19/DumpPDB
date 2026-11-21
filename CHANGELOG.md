# Changelog

## Unreleased

### Added — PDB

* **Global functions, variables, statics and public symbols** are now dumped
  (`-functions`, `-globals`, `-publics`, `-symbols`, `-dumpall`, and `-type <name>` falls back to
  them). Output includes calling convention, `__declspec` attributes, parameters with names,
  RVA / `section:offset` / size and `@ file:line`. See `docs/user/globals.md`.
* Symbols are collected from the global scope **and every compiland**, so `static` functions, file
  statics and function-local statics are found; duplicates are removed.
* `-symbols` / `PdbApi_DumpSymbolTable`: tab-separated table (kind, name, undecorated, rva, section,
  offset, length, class, compiland, source, line, flags, type, params, qualifiers).
* `DumpConfig.ShowMemberAddress`: class dumps can annotate members with their RVA.
* New settings: `ShowAddress`, `ShowSourceLine`, `ShowCallingConv`, `ShowFuncAttrs`,
  `GlobalsIncludeMembers`, `GlobalsIncludeLocalStatics`, `GlobalsSortByAddress`.
* `PdbApi_GetPdbInfo` (GUID / age / platform) and `PdbApi_ExportToFile` (large dumps straight to a file).
* `ConsoleManager::printText` prints text verbatim (a `%` in a dumped string no longer breaks output).

### Added — executables (no PDB, no disassembler)

* Real **MSVC RTTI demangler** (`MsvcDemangler`): templates with nested/non-type arguments,
  name back-references, anonymous namespaces, function-local classes, rvalue refs, function pointers.
  100 % of the 7487 distinct type names in the two sample executables are parsed (previously every
  template, ~12 % of the names, was printed as garbage such as `$0A::UDlcInstalled_t::VSteamEnvironment::?$CCallback`).
* `ExeAnalyzer`: reference index from base relocations, function discovery, strings with
  referencing functions, imports used per function, global/static objects, class → constructor /
  destructor attribution, virtual-function ownership, source files from `__FILE__` strings, names with
  confidence levels.
* `PEDirectories`: imports / delay imports / exports, CodeView **PDB path + GUID + age**, TLS, load
  config (SafeSEH / CFG), resources (version info, manifest), Rich header, section entropy.
* Commands `-exeinfo`, `-execlass`, `-exeaddr`, `-exeanalyze`; reports (CSV / JSON / text) and name
  import scripts for **IDA, Ghidra and x64dbg**. See `docs/user/exe-analysis.md`.
* All TypeDescriptors are reported, including classes that have no vftable of their own.

### Added — resources

* **`-exeresources` / `ExeApi_ExportResources` / `--exe-resources`**: full resource export — `resources.rc`,
  `resource.h`, a byte-exact `resources.res`, `icons/*.ico`, `cursors/*.cur`, `bitmaps/*.bmp`,
  `manifest/*.manifest`, `binary/*`, `resources.csv`. Decompiles ICON/CURSOR groups, BITMAP, STRINGTABLE,
  VERSIONINFO, MANIFEST, DIALOG/DIALOGEX, MENU/MENUEX, ACCELERATORS; the rest is extracted by reference.
  Also part of `-exeanalyze` (`resources/`) and `restore_project.py` (`Resources/`).

### Added — pure-Python PDB reader (no DIA, no Windows)

* `dumppdb_tools.pdbfile`: MSF container, DBI, module symbols, C13 line numbers and TPI types, rendered as
  C++ declarations and `SymbolEntry` objects. `restore_project.py --native` uses it (automatic when the DLL
  is missing). Independent of the C++/DIA implementation, so output can differ in details.

### Added — scripts

* `scripts/restore_project.py`: restore a source tree (types, globals, function skeletons, `_meta`
  address maps) from a PDB, a PDB + exe, an exe only, or a saved analysis directory (no DLL).
* `dumppdb_tools.restore` package; new `PdbClient` methods; `dump_pdb.py` options for all of the above.
* `PdbApiError` (a `RuntimeError`) carries the result code and the native last-error text.

### Fixed

* **Global functions / variables / statics were silently dropped**: `TopLevelDispatcher::dumpTopLevelAny`
  and `PdbToolset::dumpSymbolsGrouped` had an empty `default:` branch for everything but UDT / enum / typedef.
* CLI crashed (null dereference) on an unknown command or too few arguments.
* RTTI: `BaseClassInfo::isVirtual` tested bit 0 of the BCD attributes (`NOTVISIBLE`), not the virtual-base
  indicator (`pdisp != -1` / `BCD_VBOFCONTOBJ`).
* RTTI: vftable lookup was quadratic (12 s on a 28 MB executable, now 0.2 s); `findExact` rescanned and
  lower-cased every name on each call.
* RTTI: a vftable ended at the first non-code word, which could swallow neighbouring data; with
  relocations present it now ends exactly at the first slot that is not a relocated code pointer.
* PE: `rvaToOff` could return an offset into the *next* section for the zero-initialised tail of a section
  whose virtual size exceeds its raw size.
* `resolve_type_sources.collect_type_sources`: when a lookup failed, the sources of the **previous** type
  were assigned to the failed one (and the first failure raised `NameError`).
* `PdbToolset::dumpCompilandsEnv`: the VARIANT was only cleared on one branch.
* `recover_file_sources.py` pointed at a non-existent `recover_sources.py`.

### Changed

* `PdbApi_SetConfig` starts from the current configuration instead of defaults (the layout of
  `PdbApiDumpConfig` is unchanged; the new options use the separate `PdbApiGlobalsConfig`).
* `BinaryFile` opens files through `std::filesystem::path` (portable).
* `PEImage` exposes headers, data directories, bounds-checked RVA reads and the Rich header; the old
  accessors (`imageBase`, `sections`, `rvaToOff`) behave as before.

### Verification status

* Built and **run on the two sample executables** (Linux/g++): PE parsing, RTTI, demangler, analysis,
  reports. Unit tests: `tests/Unit/MsvcDemanglerTests.cpp`, `tests/Unit/RttiImageTests.cpp`.
* The **PDB/DIA code could not be built or run against a real PDB** (no DIA SDK / Windows). It was
  checked with `g++ -fsyntax-only` against a generated stub of `dia2.h` and executed against an in-memory
  mock of the DIA interfaces (`tools/linux-check`). These DIA methods are new to the code base and
  should be the first suspects if the first MSVC build complains: `IDiaSymbol::get_isNaked`,
  `get_noInline`, `get_noReturn`, `get_inlSpec`, `get_hasSEH`, `get_hasEH`, `get_hasInlAsm` (all in
  `GlobalRenderer::functionAttributes`, easy to delete), `get_function`, `get_code`,
  `get_undecoratedName`, `get_guid`, `get_age`, `get_signature`.
* `tests/Integration/GlobalsTests.cpp` (needs the TestCompiland PDB) and the Zydis-based parts
  (`FieldRecovery`, `SignatureAnalyzer`, `TypeReconstructor`) were **not** run.
