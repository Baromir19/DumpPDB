# Python Scripts

All scripts live in `scripts/` and use the `dumppdb_tools` package bundled alongside them. They talk to the PDB via `PdbAPI.dll` (the C API wrapper).

**Prerequisites:**
- Python 3.10+
- `PdbAPI.dll` built and reachable (default path: `./PdbAPI.dll`). Pass `--dll <path>` to any script to override.

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
