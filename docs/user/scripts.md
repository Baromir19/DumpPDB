# Python Scripts

All scripts live in `scripts/` and use the `dumppdb_tools` package bundled alongside them. They talk to the PDB via `PdbAPI.dll` (the C API wrapper).

**Prerequisites:**
- Python 3.10+
- `PdbAPI.dll` built and reachable (default path: `./PdbAPI.dll`). Pass `--dll <path>` to any script to override.

---

## restore_project.py

Restores a source tree from a PDB and/or an executable: types, global variables, function
skeletons with signature / RVA / size / line, and a `_meta/` directory with RVA → file:line →
name maps. It is independent of the older `resolve_type_sources` / `reconstruct_sources`
pipeline (which depends on a game-specific template) and needs no database.

```
# PDB: best result (types, signatures with parameter names, globals, source lines)
python scripts/restore_project.py --dll PdbAPI.dll --pdb Game.pdb --out restored

# PDB + EXE: also recovers path case from the exe, checks that the PDB belongs to the exe,
# and writes IDA / Ghidra / x64dbg name scripts
python scripts/restore_project.py --dll PdbAPI.dll --pdb Game.pdb --exe Game.exe --out restored

# EXE only: RTTI classes, function stubs and the file layout (no PDB needed)
python scripts/restore_project.py --dll PdbAPI.dll --exe Game.exe --out restored

# From an analysis directory written earlier -- no native library at all
python scripts/dump_pdb.py --exe Game.exe --exe-analyze analysis      # once
python scripts/restore_project.py --exe-analysis analysis --out restored
```

Output:

```
restored/<original layout>/Foo.h      types
restored/<original layout>/Foo.cpp    variables + function skeletons (// RVA, size, line; TODO bodies)
restored/_external/...                code outside the project root (CRT, SDK, third party)
restored/_unassigned/unassigned.cpp   symbols whose file could not be determined
restored/Resources/                   resources.rc, resource.h, resources.res, icons/, manifest/ ... (when an exe analysis is given)
restored/_meta/                       symbols.tsv, function_map.csv, variable_map.csv, publics.txt,
                                      restore_report.txt, pdb_names_ida.py / _ghidra.py / _x64dbg.dd32.json
```

How files are chosen:

* **Project root** is where the source tree first forks (several sibling directories each holding
  ≥ 10 % of the files); small outliers such as the CRT are ignored and filed under `_external`.
  Override with `--root`. The detected root is printed in `_meta/restore_report.txt`.
* **Path case** is recovered from every spelling of the path found in the PDB and in the exe's
  embedded `__FILE__` strings (the most capitalised spelling wins).
* **Functions** go to the file of their first source line; without line info, to the file most of
  their object file's functions came from, then to the file of their class, then `_unassigned`.
* **Types** go to a header they were seen in (or the `.cpp` if only seen there); members without line
  info are implemented next to their type. EXE-only classes get `<stem>.h` next to their `.cpp`.
* Template instances are written as comments (their definition lives in a header); functions inside
  an anonymous namespace are wrapped in `namespace { }`; compiler-generated functions are skipped
  (`--include-compiler-generated` keeps them).

Bodies are **not** recovered; every function is a signature plus `// TODO`. With `--exe` and no
PDB, signatures are unknown and functions are listed as comments.

`--check-match strict|warn|off` controls what happens when the PDB's GUID differs from the exe's;
name scripts are skipped for a mismatching PDB because the RVAs would be wrong.

Tests: `python -m unittest discover -s tests` from `scripts/` (pure Python, no DLL).

---

## dump_pdb.py

General-purpose PDB query tool.

```
python scripts/dump_pdb.py --pdb <file.pdb> [options]
```

| Option | Description |
|---|---|
| `--type <name>` | Dump a type by name (case-insensitive). |
| `--class <name>` | Dump a class/struct/union by name. |
| `--enum <name>` | Dump an enum by name. |
| `--typedef <name>` | Dump a typedef by name. |
| `--nested-types <name>` | List nested type names of a type. |
| `--source-files <name>` | List source files for a type. |
| `--enumerate-files` | List all source files in the PDB. |
| `--symbols` | Enumerate all top-level symbol names. |
| `--all-types` | With `--symbols`: include nested types too. |
| `--case-sensitive` | Use case-sensitive name lookup. |
| `--template-params` | When dumping a template instantiation, emit `template<...>` and substitute concrete args with parameter names. |
| `--file <path>` | Binary file to search (exe/dll/bin). |
| `--strings` | Search for strings in `--file`. |
| `--min-length N` | Minimum string length (default: 4). |
| `--encodings a,b` | Comma-separated encodings: `ascii`, `utf8`, `utf16` (default: `ascii,utf16`). |
| `--sections .x,.y` | PE sections to scan (default: all string-candidate sections). |
| `--regex <pattern>` | Regex filter for string results. |
| `--signatures <pattern>` | Search for a byte signature in `--file` (e.g. `"FF ?? 01 BD"`). |
| `--dll <path>` | Path to `PdbAPI.dll` (default: `./PdbAPI.dll`). |

**Examples:**

```
python scripts/dump_pdb.py --pdb Dev.pdb --type Actor
python scripts/dump_pdb.py --pdb Dev.pdb --symbols
python scripts/dump_pdb.py --pdb Dev.pdb --type "TList<int>" --template-params
python scripts/dump_pdb.py --file Game.exe --strings --min-length 6 --encodings ascii
python scripts/dump_pdb.py --file Game.exe --signatures "FF ?? 01 BD ?? CA"
```

---

### New `dump_pdb.py` options

| Option | Purpose |
|---|---|
| `--functions [FILTER]`, `--globals [FILTER]`, `--publics [FILTER]` | List global functions / variables / public symbols |
| `--include-members`, `--local-statics`, `--sort-by-address`, `--no-address` | Tune those listings |
| `--member-addresses` | Annotate members with their RVA in class dumps |
| `--symbol-table OUT.tsv`, `--dump-all OUT.txt` | Write the full symbol table / everything to a file |
| `--pdb-info` | GUID / age / platform of the PDB (compare with `--exe-info`) |
| `--exe-info`, `--exe-class NAME`, `--exe-addr [rva:\|va:]HEX`, `--exe-resources DIR`, `--exe-analyze DIR` | Executable analysis (requires `--exe`) |

---

## resolve_type_sources.py

For every type in a PDB, find its best-matching source file using a scoring algorithm and write the results to a SQLite database. Useful as the first step in a source-reconstruction pipeline.

```
python scripts/resolve_type_sources.py --pdb <file.pdb> [options]
```

| Option | Default | Description |
|---|---|---|
| `--pdb` | required | PDB file. |
| `--source-root` | — | Root directory of the actual source tree to match against. |
| `--db` | `sources.db` | Output SQLite database. |
| `--output` | `pdb_type_sources.txt` | Plain-text report of resolved mappings. |
| `--exclude` | `exclude_types.txt` | File with type-name patterns to skip. |
| `--dll` | `./PdbAPI.dll` | Path to `PdbAPI.dll`. |

---

## reconstruct_sources.py

Renders per-type source snippets from a template and the resolved source database. Uses a small DSL for template expansion.

```
python scripts/reconstruct_sources.py --pdb <file.pdb> --template <template.txt> [options]
```

The template DSL supports:
- `<PATH>` — variable reference.
- `!IF <path>` ... `!END` — conditional block.
- `!FOR <var> IN <path>` ... `!END` — loop.
- Lines starting with `#` are comments.

See `scripts/reconstruction_type.example.txt` for a concrete example.

---

## recover_file_sources.py

Reconstructs the original source file tree from PDB paths and actual binary strings found in the executable. Useful for recovering correct capitalization and directory layout when PDB paths differ from the real filesystem.

```
python scripts/recover_file_sources.py --pdb <file.pdb> --exe <file.exe> --path-prefix <prefix> [options]
```

| Option | Default | Description |
|---|---|---|
| `--pdb` | required | PDB file. |
| `--exe` | required | Executable/DLL to scan for embedded path strings. |
| `--path-prefix` | required | Only process source paths beginning with this prefix. |
| `--output` | `reconstructed` | Output directory. |
| `--output-extension` | `.hpp` | Extension to assign to recovered files. |
| `--dll` | `./PdbAPI.dll` | Path to `PdbAPI.dll`. |

---

## verify_sources.py

GUI tool for reviewing and manually correcting type → source-file mappings stored in a SQLite database.

```
python scripts/verify_sources.py --db <sources.db>
```

Displays each type with its candidate source files and confidence scores. Lets you accept, reject, or reassign mappings interactively.

---

## compare_type_sources.py

Cross-reference type → source-file mappings from the SQLite database against a Visual Studio Browse database (`.VC.db`) to detect discrepancies.

```
python scripts/compare_type_sources.py --db <sources.db> --browse <path/to/.VC.db>
```

---

## disable_missing_types.py

Mark types in the SQLite database as disabled if they no longer exist in the PDB. Keeps the database clean when working across multiple PDB versions.

```
python scripts/disable_missing_types.py --db <sources.db> --pdb <file.pdb>
```

---

## dumppdb_tools package

All scripts share the `dumppdb_tools` Python package in `scripts/dumppdb_tools/`. It wraps `PdbAPI.dll` via `ctypes` (`pdb_client.py`) and provides:

- `PdbClient` — low-level DLL wrapper.
- `SourceDatabase` — SQLite schema and query helpers.
- `TypeAggregator` — collects types from the PDB.
- `resolve_best` / `scorer` — source-file scoring and resolution logic.
- `SourceTree` / filesystem helpers — path normalization and case recovery.
