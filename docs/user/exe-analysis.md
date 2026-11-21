# Executable analysis

DumpPDB can analyse a PE executable **without a PDB and without a disassembler**.
Everything below comes from three things a PE file gives away for free:

1. **Base relocations.** Every absolute pointer stored in code or data is listed in
   `.reloc`. Reading the value at each entry gives an exact cross-reference index
   ("this instruction / data word refers to that address").
2. **Function padding.** MSVC aligns functions and pads them with `0xCC`.
3. **RTTI.** The compiler's run-time type information names every polymorphic class and
   lists its virtual functions.

x86 and x64 images are supported; the `.reloc` section must be present (it is, unless the
image was linked with `/FIXED`; without it the analysis still runs but is less precise).

---

## Commands

| Command | What it does |
|---|---|
| `-exeinfo <file.exe>` | PE summary: timestamp, **PDB path / GUID / age**, toolchain, sections with entropy, imports per DLL, RTTI and analysis statistics. |
| `-execlass <name> <file.exe>` | One class: hierarchy, every vftable with **named slots**, constructors / destructors, static instances, source files, strings and imports its functions use. |
| `-exeaddr [rva:\|va:]<hex> <file.exe>` | What is at an address and who references it (see *Addresses* below). |
| `-exeresources <outdir> <file.exe>` | Extracts the **resources**: `.rc` script, icons, manifest, `.res` (see below). |
| `-exeanalyze <outdir> <file.exe>` | Writes every report and the disassembler scripts into `outdir` (including `resources/`). |
| `-exetypes <file.exe>` / `-exetype <name> <file.exe>` | Existing: list RTTI types / reconstruct a type with fields (needs the disassembler). |

```
DumpPDB.exe -exeinfo Game.exe
DumpPDB.exe -execlass "Ns::Derived" Game.exe
DumpPDB.exe -exeaddr va:00915020 Game.exe
DumpPDB.exe -exeanalyze analysis Game.exe
```

Python: `PdbClient.exe_info()`, `exe_class_info(name)`, `exe_describe_address(addr, kind)`,
`exe_export_analysis(dir)`; or `dump_pdb.py --exe Game.exe --exe-info | --exe-class N | --exe-addr A | --exe-analyze DIR`.

### Addresses

`-exeaddr` accepts a virtual address (what debuggers and IDA show) or an RVA. A value can be
valid as **both**: in an image based at `0x400000` that is 29 MB long, `0x915020` is a VA
(RVA `0x515020`) *and* an RVA inside `.text`. The default (`auto`) reads such a value as a VA
and prints a note saying so; prefix `rva:` or `va:` to be explicit. The first line of the
output always shows both forms so you can see what was used.

---

## What `-exeanalyze` writes

| File | Content |
|---|---|
| `summary.txt` | Same as `-exeinfo`. |
| `imports.txt` | Imports (and exports) with the number of references each one has. |
| `classes.txt` / `classes.csv` | Every class: bases, vftables, ctor / dtor / deleting-dtor addresses, best source file. |
| `vtables.csv` | Every vftable slot: class, slot, function RVA, function name. |
| `hierarchy.txt` / `hierarchy.dot` | Inheritance tree (text) and Graphviz graph. |
| `functions.csv` | Every function found: RVA, size, discovery evidence, callers, name, name confidence, owning class, source file. |
| `strings.csv` | Every ASCII / UTF-16 string, who references it, the embedded source path if any. |
| `globals.csv` | Referenced writable globals/statics: kind, class (for static instances), size bound, accessing functions. |
| `source_files.txt`, `source_spellings.txt`, `source_map.csv` | Source paths embedded by `assert` / `__FILE__`, every spelling (for case recovery) and which classes use each. |
| `resources/` | `.rc`, `resource.h`, `.res`, icons, cursors, bitmaps, manifest (see above). |
| `names.json` | All names with RVA, kind, size, confidence. |
| `apply_names_ida.py`, `apply_names_ghidra.py`, `x64dbg_labels.dd32.json` | Name import for IDA, Ghidra and x64dbg. |

### Resources: `.rc`, icons, manifest and the rest

`-exeresources` (also done by `-exeanalyze` into `resources/`) rebuilds the resource script:

```
resources/
  resources.rc            the script (UTF-8, #pragma code_page(65001))
  resource.h              #defines for the numeric ids
  resources.res           BYTE-EXACT compiled copy of every resource
  resources.csv           index: type, name, language, size, RVA
  icons/*.ico             one .ico per RT_GROUP_ICON, assembled from its RT_ICON images
  cursors/*.cur           same for cursors, hotspot preserved
  bitmaps/*.bmp           DIBs with a BITMAPFILEHEADER
  manifest/*.manifest     the embedded application manifest
  binary/*                RCDATA, HTML, FONT, MESSAGETABLE, user-defined types, anything else
```

Decompiled into readable script: `ICON`, `CURSOR`, `BITMAP`, `STRINGTABLE` (per language),
`VERSIONINFO`, manifest, `DIALOG`/`DIALOGEX` (controls as `CONTROL` with decoded `WS_`/`BS_`/`SS_`/
`ES_`/`LBS_`/`CBS_` styles), `MENU`/`MENUEX`, `ACCELERATORS`. Everything else is extracted as a file and
included by reference. A resource that cannot be decompiled is kept as a raw file, listed as a note
in the log, and is still in `resources.res`.

```
rc /c65001 resources.rc          # rebuild from the script
link ... resources.res           # or link the exact copy
```

Notes: names of ids are generated (`IDI_103`, `IDS_STRING_104`, `IDC_CTRL_5`) since the executable does not
keep symbolic names; `rc` renumbers the `RT_ICON` images when it rebuilds, which is why `resources.res`
is provided for an exact copy. Text is exported exactly as stored: the sample final build's Russian
and French strings are already corrupted inside the executable (`пїЅ`, `ï¿½`).

### Importing names into a disassembler

* **IDA:** *File ▸ Script file…* → `apply_names_ida.py`. It uses the image base of the
  database, so rebasing is fine. Edit `MIN_CONFIDENCE` (1 = low … 3 = high) to skip guesses.
* **Ghidra:** *Script Manager* → run `apply_names_ghidra.py` (Jython).
* **x64dbg:** import `x64dbg_labels.dd32.json` through *File ▸ Import database*.

Names are valid identifiers (`Ns_Derived_vfunc_2`); the readable name (`Ns::Derived::vfunc_2`) is
in the comment.

---

## How names are produced (and how far to trust them)

| Name | Evidence | Confidence |
|---|---|---|
| `Class::vftable`, `…RTTI_CompleteObjectLocator`, `…RTTI_TypeDescriptor` | RTTI structures | high |
| `Class::vfunc_N` | slot N of the vftable of the class where the function first appears | medium |
| `Class::scalar_deleting_dtor` | slot 0 whose code tests the "delete" flag bit | medium |
| `Class::dtor` | called by the deleting destructor and stores the class vptr | medium |
| `Class::ctor`, `ctor_N` | stores the vptr of the class (see below) | low |
| `shared_vfunc_XXXX` | the same code is used by unrelated classes (identical-code folding or a trivial stub) | low |
| `entry_point`, `tls_callback_*`, export names | PE headers | high |

**Where a virtual function is named.** For every class and slot the analysis walks up the
hierarchy to the most basic class whose own vftable holds the same function in the same slot.
`Derived::f1` that is inherited is therefore named `Base::vfunc_1`.

**Constructors.** A function that stores a class's vftable address is a constructor *or*
destructor. A function that stores the vptrs of several related classes (an inlined base
constructor stores the base vptr first) belongs to the *most derived* one. A function that
stores unrelated classes' vptrs is attributed to nobody.

**Source files.** Assert macros embed `__FILE__`; a string reference from inside a function
ties that function to a file, and virtual functions tie classes to files. `.cpp` files weigh
more than headers, and headers referenced by many classes (`Singleton.h`, inlined templates) are
down-weighted. A release build with asserts compiled out has few such strings (the sample
final build has 46 paths versus 3011 in the dev build).

### Measured on the two sample executables

| | `L_A_-Noire_Release-Dev.exe` | `LANoire.exe` (final) |
|---|---|---|
| Linker / toolchain | 9.0 / VS2008 + VS2010 libs | same |
| Classes (vftables) | 7135 (7785) | 6956 (7563) |
| RTTI names demangled | 100 % (570 templates, 71 function-local, 20 anonymous-namespace) | 100 % |
| Functions found | 82 142 | 58 610 |
| vftable targets found *without RTTI help* | 98.3 % | 95.4 % |
| Deleting destructors recognised | 6386 / 7135 | 6821 / 6956 |
| Source paths recovered | 3 048 | 46 |
| Analysis time (whole image) | 0.6 s | 0.4 s |

The "vftable targets" figure is a real accuracy measurement: vftable slots are known
function starts, so it shows how many starts the padding/prologue heuristic finds on its own.
The remaining few percent are tightly packed leaf functions with no padding.

---

## Limits

* **Function boundaries are heuristic.** Functions that are neither padded nor preceded by a
  `ret`/`jmp` are missed or merged with a neighbour. Function sizes stop at the next start.
* **Constructors are guesses.** Optimised code may inline a constructor completely; a `ctor`
  name only says "this function stores the vptr".
* **No signatures.** Parameters and return types are not recoverable from RTTI. Use a PDB
  (`restore_project.py --pdb`) for those.
* **Pure virtual functions.** Slots of pure virtuals point at a stub (`_purecall`). It is only
  recognised when the stub references a "pure virtual" message string. The sample game does not
  use `= 0` at all. The usual `push 19h` test gave false positives in ordinary functions and
  was dropped.
* **Static instances.** Objects whose vptr is initialised by a constructor at start-up (the
  common case) are zero in the file and cannot be found; only constant-initialised objects are
  listed as "object" globals.
* **Packed/encrypted sections.** The sample final build has two sections with entropy 8.0
  (`.rkstr`, `.tbm`), i.e. compressed or encrypted data. Code and RTTI are intact there, but data
  stored in those sections is not analysable. `-exeinfo` flags such sections. (A high-entropy
  `.rsrc` is *not* a sign of encryption: PNG-compressed icons alone give ~7.9.)
* **Matching a PDB.** Compare the GUID/age printed by `-exeinfo` with `--pdb-info` before using
  RVAs from a PDB on an executable; `restore_project.py` does this for you.
