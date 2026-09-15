# CLI Usage

```
DumpPDB.exe <command> [args...] <file.pdb>
```

`file.pdb` is always the last argument. A relative path is resolved against the current working directory.

---

## Commands

### `-type <typename>`

Dump a type (class, struct, union, enum, or typedef) by name.

```
DumpPDB.exe -type Actor Dev.pdb
DumpPDB.exe -type "TList<int>" Dev.pdb
```

The output includes field offsets, sizes, access specifiers, virtual functions, and the source files where the type was seen. Output is also copied to the clipboard (unless disabled in settings).

**Template instantiations** — wrap the name in quotes when it contains `<` or `>`, since the shell treats those as redirection operators:

```
DumpPDB.exe -type "TemplateType<SomeArg>" Dev.pdb
```

The search is case-insensitive. If no exact match is found, a namespace-prefix fallback is tried (e.g. searching for `Actor` will also find `Game::Actor`). Multiple matches in the same namespace are grouped into one `namespace { ... }` block.

---

### `-compilands [true]`

List all compilation units (`.obj` files) linked into the PDB.

```
DumpPDB.exe -compilands Dev.pdb
```

Pass `true` to include compiler details and environment variables per compiland:

```
DumpPDB.exe -compilands true Dev.pdb
```

---

### `-sources`

List all source file paths recorded in the PDB.

```
DumpPDB.exe -sources Dev.pdb
```

---

### `-settings [-h] <setting> <value>`

Read or write a persistent setting in `config.ini`.

```
# List all settings with current values
DumpPDB.exe -settings -h

# Change a setting
DumpPDB.exe -settings DumpConfig.ShowOffset false
```

The change is saved immediately to `config.ini` next to the executable. See [settings.md](settings.md) for the full list.

---

### `-help` / `--h`

Print the command table.

```
DumpPDB.exe -help
```

---

## Output format

A typical `-type` output looks like:

```cpp
// size: 52 byte
class MainObjectDefinition : public ExposedDefinition
{
    /// TYPEDEFS:
    typedef ClassBeingDefined MainObject;

    /// VIRTUALS:
    virtual MainObject* CreateObject(); // 0x0
    virtual void Init(); // 0x0

    /// FUNCS:
    void MainObjectDefinition();
    MainObjectDefinition& operator=();
};
// f:\user\sample\mainobject.cpp
// f:\user\sample\maininstance.cpp
```

- Source file paths are printed below the type body when `ShowTypeSource` is enabled.
- Offsets next to virtual functions are relative to the vtable slot.
- Compiler-generated helpers (`__local_vftable_ctor_closure`, default copy constructors, etc.) are hidden by default (`HideCompilerGenerated = true`).
- When a function argument name is not recorded in the PDB, the full argument type is emitted instead, followed by `<-`.
