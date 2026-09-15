# Settings Reference

Settings are stored in `config.ini` next to the executable. The file is created automatically on first run with default values. All settings survive across runs.

You can edit the file directly or use the CLI:

```
DumpPDB.exe -settings <setting> <value>
DumpPDB.exe -settings -h          # list all settings with current values
```

---

## DumpConfig section

Controls how types are formatted in the output.

| Setting | Type | Default | Description |
|---|---|---|---|
| `DumpConfig.ShowSize` | bool | `true` | Print `// size: N byte` above each type. |
| `DumpConfig.ShowOffset` | bool | `true` | Print byte offsets next to each field. |
| `DumpConfig.ShowAccess` | bool | `true` | Emit `public:` / `protected:` / `private:` labels. |
| `DumpConfig.ShowInfoComment` | bool | `false` | Print extra metadata comments inside the type body. |
| `DumpConfig.ShowNonScoped` | bool | `true` | Strip the enclosing class prefix from nested type names (show `Inner` instead of `Outer::Inner`). |
| `DumpConfig.ShowEnumHex` | bool | `false` | Print enum values in hexadecimal (`0x1A`) instead of decimal. |
| `DumpConfig.ShowTypeSource` | bool | `false` | Print the source file paths where the type was seen, below the type body. |
| `DumpConfig.CurlyBraceNewline` | bool | `true` | Place the opening `{` on a new line (Allman style). |
| `DumpConfig.HideCompilerGenerated` | bool | `true` | Suppress compiler-generated helpers like `__local_vftable_ctor_closure`, default constructors, and copy operators. |
| `DumpConfig.TemplateParams` | bool | `false` | When dumping a template instantiation (e.g. `-type "TList<int>"`), emit a `template<typename T>` header and substitute the concrete arguments with generated parameter names. |
| `DumpConfig.BaseAccessType` | uint | `0` | Override the access specifier for all members (`0` = use the symbol's own access, `1` = private, `2` = protected, `3` = public). |
| `DumpConfig.IntStyle` | int | `1` | Integer type style: `0` = MSVC native (`__int32`, `__int64`), `1` = C stdint (`int32_t`, `int64_t`). |

---

## CommandConfig section

| Setting | Type | Default | Description |
|---|---|---|---|
| `CommandConfig.UseClipboard` | bool | `true` | Copy `-type` output to the clipboard automatically. |

---

## config.ini format

The file uses standard INI syntax. Comments start with `;` or `#`. Boolean values are stored as `true`/`false`.

```ini
[DumpConfig]
ShowSize=true
ShowOffset=true
ShowAccess=true
ShowInfoComment=false
ShowNonScoped=true
ShowEnumHex=false
ShowTypeSource=false
CurlyBraceNewline=true
HideCompilerGenerated=true
TemplateParams=false
BaseAccessType=0
IntStyle=1

[CommandConfig]
UseClipboard=true
```
