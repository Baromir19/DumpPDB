# Global functions, variables, statics and public symbols

Besides types, a PDB describes everything that lives at an address: free functions, global
variables, `static` data, static class members, function-local statics and "public" symbols
(a decorated name and an address, nothing else). Earlier versions of DumpPDB only printed
types and **silently dropped all of these**; the commands below print them.

| Command | Lists |
|---|---|
| `-functions [filter]` | Free functions grouped by namespace |
| `-globals [filter]` | Global variables, file statics, constants |
| `-publics [filter]` | Public symbols (works on stripped PDBs) |
| `-symbols <out.tsv>` | Every function / variable / thunk / public as a tab-separated table |
| `-dumpall <out.txt>` | All types **and** all of the above, to a file |
| `-type <name>` | A name that is not a type now finds functions / variables too |

`filter` is a case-insensitive substring of the qualified name. The PDB path is the last
argument as usual:

```
DumpPDB.exe -functions Update Dev.pdb
DumpPDB.exe -globals g_ Dev.pdb
DumpPDB.exe -symbols symbols.tsv Dev.pdb
```

## Output

Illustrative example (the format is exact; the symbols are made up):

```cpp
// 4 functions
static void __cdecl helper(int32_t);  // RVA 0x00001300 [0001:00000300] len 0x8
int32_t __stdcall main();  // RVA 0x00001100 [0001:00000100] len 0x20
__declspec(naked) void __cdecl naked_fn(int32_t);  // RVA 0x00001400 [0001:00000400] len 0x8

namespace Game
{
    void __cdecl Update(int32_t dt);  // RVA 0x00001000 [0001:00000000] len 0x40  @ e:\src\game.cpp:42
}
```

```cpp
// 3 variables
int32_t g_count;  // RVA 0x00002000 [0002:00000000] len 0x4
static int32_t s_cache;  // RVA 0x00002020 [0002:00000000] len 0x4
```

* `RVA`, `[section:offset]` and `len` (code length, or the size of a variable's type) locate the
  symbol in the executable; `@ file:line` is the first source line of a function.
* `static` marks a function that is only visible inside one object file (it exists only in that
  object file's symbol table) and file-scope variables.
* Calling conventions are printed for x86 PDBs. Member functions are not annotated with
  `__thiscall` (it is the default); x64 PDBs have one convention and print none.
* Functions without type information print `/* signature unavailable */`.
* Public symbols print the undecorated signature and keep the decorated name in a comment:
  `void __cdecl Baz(void);  // RVA 0x00003000 [0001:00002000]  [public-function]  decorated: ?Baz@@YAXXZ`.

## Where the symbols are found

A PDB stores some symbols in its global stream and the rest per object file (*compiland*).
`static` functions, file statics and function-local statics exist **only** in the latter, so every
listing walks the global scope **and every compiland** and removes duplicates (same tag, name and
address). A function that appears only in a compiland is marked `static`.

## Settings

| Key | Default | Effect |
|---|---|---|
| `DumpConfig.ShowAddress` | `true` | `// RVA … len …` on functions and variables |
| `DumpConfig.ShowSourceLine` | `true` | `@ file:line` on functions (needs line info) |
| `DumpConfig.ShowCallingConv` | `true` | Calling convention (x86) |
| `DumpConfig.ShowFuncAttrs` | `true` | `__declspec(naked/noinline/noreturn)`, `inline`, body flags `[seh,eh,inline-asm]` |
| `DumpConfig.ShowMemberAddress` | `false` | Annotate members **inside class dumps** with their RVA — a class dump doubles as an address map |
| `DumpConfig.GlobalsIncludeMembers` | `false` | Also list out-of-line member definitions and static data members |
| `DumpConfig.GlobalsIncludeLocalStatics` | `false` | Also list function-local statics (`[static-local in Func]`) |
| `DumpConfig.GlobalsSortByAddress` | `false` | Sort by RVA instead of by name |

Class fields, locals and parameters are never listed as variables.

## Symbol table (TSV)

`-symbols` / `PdbApi_DumpSymbolTable` write one line per symbol, header first, UTF-8, tab separated:

| Column | Meaning |
|---|---|
| `kind` | `function`, `thunk`, `global`, `file-static`, `static-member`, `static-local`, `constant`, `public-function`, `public-data` |
| `name` | Qualified name as stored in the PDB |
| `undecorated` | Full undecorated signature (publics) |
| `rva`, `section`, `offset` | Location (`0x…`, empty when the symbol has no address) |
| `length` | Code length (functions) or type size (variables) |
| `class` | Owning class for members |
| `compiland` | Object file |
| `source`, `line` | Source file and first line (functions) |
| `flags` | Comma separated: `virtual`, `static`, `pure`, `file-local`, `compiler-generated`, `attrs`, `seh`, `eh`, `inline-asm`, calling convention |
| `type` | Variables: `type name`. Functions: return type (empty for constructors, destructors, conversion operators) |
| `params`, `qualifiers` | Functions: parameter list (with names) and `const` / `volatile` / `noexcept` |

`scripts/dumppdb_tools/restore/symbols.py` parses it into `SymbolEntry` objects.
