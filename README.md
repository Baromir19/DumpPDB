![DumpPDB banner](/images/readme_header.png)

# About
The tool was developed on DIA API, with the main goal of speeding up the output of class fields and other types.
Its functionality includes outputting the type by name (`-type`), outputting the compilands (`-compilands`), and source files (`-sources`).

In particular, this project was written as the best replacement for `IDA` (of course, only in the implementation of the types, as decompilation is too time-consuming a task), `pdbex` and `Dia2Dump`, which do not provide a normal and fast way to implement code (each has its drawback, and I tried to address them here).

# Usage
## Type
Go to the path of DumpPDB, then enter `DumpPDB.exe -type <type_name> <source.pdb>`

In the output, you may observe a considerable amount of debug information, such as offsets of fields and bit fields, sizes of structures, as well as the source files in which this structure was encountered. 
While all of this information is useful, it may also be a distraction for you. 
If you so desire, you can recompile the code by altering the global parameters, or you can await the implementation of serialization.

**Example output:**
``` cpp
// size: 52 byte
class MainObjectDefinition : public ExposedDefinition
{
    /// TYPEDEFS:
    typedef ClassBeingDefined MainObject;

    /// VIRTUALS:
    virtual MainObject* CreateObject(); // 0x0
    virtual void Init(); // 0x0
    virtual void ~MainObjectDefinition(); // 0x0
    virtual void* __vecDelDtor(); // 0x0

    /// FUNCS:
    void MainObjectDefinition();
    void MainObjectDefinition();
    MainObjectDefinition& operator=();
    void __local_vftable_ctor_closure();
};
// f:\user\sample\mainobject.cpp
// f:\user\sample\mainobject.cpp
// f:\user\sample\maininstance.cpp
// f:\user\sample\maininstance.cpp
// f:\user\sample\maininstance.cpp
// f:\user\sample\maininstance.cpp
// f:\user\sample\maininstance.cpp
// f:\user\sample\maininstance.cpp
```

# Goals
- complete the serialization
- expanded function type output
- implement a one-time output of the field's scope
- hints on function generation by the compiler
- cleanup of the "void" type for the destructor
- code cleanup

# Possible problems
You may encounter a command line limitations: `> was unexpected at this time.` or `The system cannot find the file specified.` 
You can encounter this when entering a template type, but `<` controls input and output in the command line; to solve the problem, it is enough to simply enter the above type in quotes, for example `"TemplateType<SizeS>"`.
