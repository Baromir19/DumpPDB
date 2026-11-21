"""Tests for the restoration pipeline. Pure Python: no DLL, no Windows.

Run from the scripts/ directory::

    python -m unittest discover -s tests -v
"""

import ctypes
import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dumppdb_tools.api.pdb_api import PdbApiError, PdbApiGlobalsConfig, read_string_call
from dumppdb_tools.pdb_client import PdbClient
from dumppdb_tools.restore import (
    ExeAnalysisData,
    PdbIdentity,
    ProjectBuilder,
    check_pdb_matches_exe,
    collect_names,
    detect_root,
    parse_pdb_info,
    parse_symbol_table,
    split_qualified,
    write_project,
)

HERE = Path(__file__).resolve().parent
SAMPLE_TSV = (HERE / "sample_symbols.tsv").read_text(encoding="utf-8")

FOO_TEXT = "class Foo\n{\npublic:\n    static int32_t s_count;\n    void Bar(int32_t);\n};"


def make_analysis_dir(root: Path) -> Path:
    d = root / "analysis"
    d.mkdir()
    (d / "classes.csv").write_text(
        "class,type_descriptor_rva,primary_vftable_rva,vftable_count,slot_count,bases,"
        "ctor_rvas,dtor_rva,deleting_dtor_rva,source\n"
        "Action,0x00001000,0x00002000,1,3,,0x00003000,,,e:\\Perforce\\game\\Code\\Actions/Action.cpp\n"
        "DoThing,0x00001100,0x00002100,1,4,Action,0x00003100;0x00003200,0x00003300,0x00003400,"
        "e:\\perforce\\GAME\\code\\actions/dothing.cpp\n"
        "Thing,0x00001200,0x00002200,1,1,,,,,e:\\perforce\\game\\code\\other/thing.cpp\n", encoding="utf-8")
    (d / "functions.csv").write_text(
        "rva,size,origin,callers,data_refs,name,confidence,class,source,imports_used,strings_used\n"
        "0x00003000,40,1,2,0,Action::ctor,1,Action,e:\\Perforce\\game\\Code\\Actions/Action.cpp,0,1\n"
        "0x00003400,30,1,0,1,DoThing::scalar_deleting_dtor,2,DoThing,e:\\perforce\\GAME\\code\\actions/dothing.cpp,0,0\n"
        "0x00009000,10,1,0,0,,0,,,0,0\n", encoding="utf-8")
    (d / "source_spellings.txt").write_text(
        "e:\\Perforce\\game\\Code\\Actions/Action.cpp\n"
        "e:\\perforce\\GAME\\code\\actions/dothing.cpp\n"
        "E:\\Perforce\\Game\\Code\\Actions/DoThing.cpp\n"
        "e:\\perforce\\game\\code\\other/thing.cpp\n", encoding="utf-8")
    (d / "summary.txt").write_text(
        "PDB path        : e:\\build\\game.pdb\nPDB GUID        : 3D6B1077-399B-4CD2-9A3E-8CD6F51161E6\n"
        "PDB age         : 2\n", encoding="utf-8")
    return d


class SymbolParsing(unittest.TestCase):
    def test_split_qualified_respects_templates_and_anonymous_namespace(self):
        self.assertEqual(split_qualified("A::B::f<C::D>"), ("A::B", "f<C::D>"))
        self.assertEqual(split_qualified("`anonymous namespace'::Foo::bar"),
                         ("`anonymous namespace'::Foo", "bar"))
        self.assertEqual(split_qualified("main"), ("", "main"))
        self.assertEqual(split_qualified("operator()(int)"), ("", "operator()(int)"))

    def test_parse_table(self):
        syms = parse_symbol_table(SAMPLE_TSV)
        by = {s.name: s for s in syms}
        upd = by["Game::Update"]
        self.assertEqual(upd.rva, 0x1000)
        self.assertEqual(upd.length, 64)
        self.assertEqual(upd.line, 42)
        self.assertEqual(upd.params, "int32_t dt")
        self.assertEqual(upd.calling_convention, "__cdecl")
        self.assertEqual(upd.written_calling_convention, "")  # default: not written
        self.assertEqual(by["main"].written_calling_convention, "__stdcall")
        self.assertTrue(by["helper"].is_file_local)
        self.assertTrue(by["Foo::Bar"].is_member)
        self.assertEqual(by["g_count"].kind, "global")
        self.assertTrue(by["?Baz@@YAXXZ"].is_public)

    def test_parse_tolerates_old_dll_with_fewer_columns(self):
        line = "function\tf\t\t0x00000010\t1\t0x10\t4\t\t\t\t\t\tint"
        syms = parse_symbol_table(line)
        self.assertEqual(len(syms), 1)
        self.assertEqual(syms[0].params, "")

    def test_definition_header(self):
        by = {s.name: s for s in parse_symbol_table(SAMPLE_TSV)}
        self.assertEqual(by["Game::Update"].definition_header(), "void Game::Update(int32_t dt)")
        self.assertEqual(by["main"].definition_header(), "int32_t __stdcall main()")
        self.assertEqual(by["helper"].definition_header(), "static void helper(int32_t)")


class Paths(unittest.TestCase):
    def test_detect_root_ignores_outliers(self):
        # A real project: one trunk, a fork at "game", and a few outliers (CRT, SDK).
        paths = [f"e:\\p\\game\\engine\\e{i}.cpp" for i in range(40)]
        paths += [f"E:\\P\\Game\\game\\g{i}.cpp" for i in range(30)]
        paths += [f"e:/p/game/util/u{i}.h" for i in range(25)]
        paths += ["f:\\crt\\src\\z.c", "f:\\sdk\\inc\\a.h", "f:\\sdk\\inc\\b.h"]
        self.assertEqual([p.lower() for p in detect_root(paths)], ["p", "game"])

    def test_detect_root_stops_at_a_fork_and_keeps_sibling_branches(self):
        # Mirrors a real game PDB: Code/{LANoire 75%, CTG 11%, Engine 9.5%, Util 4.5%}.
        paths = ([f"e:\\Perforce\\lanoire\\Shared\\Code\\LANoire\\a{i}.cpp" for i in range(75)]
                 + [f"e:\\Perforce\\lanoire\\Shared\\Code\\CTG\\b{i}.cpp" for i in range(11)]
                 + [f"e:\\Perforce\\lanoire\\Shared\\Code\\Engine\\c{i}.cpp" for i in range(10)]
                 + [f"e:\\Perforce\\lanoire\\Shared\\Code\\Util\\d{i}.cpp" for i in range(4)]
                 + ["f:\\vc\\crt\\x.c"])
        self.assertEqual(detect_root(paths), ["Perforce", "lanoire", "Shared", "Code"])

    def test_case_recovery_and_external(self):
        b = ProjectBuilder()
        b.add_known_paths(["e:\\Perforce\\Game\\Code\\Actions\\DoThing.cpp",
                           "e:\\perforce\\game\\code\\actions\\dothing.cpp",
                           "e:\\perforce\\game\\code\\actions\\other.cpp"]
                          + [f"e:\\perforce\\game\\code\\actions\\f{i}.cpp" for i in range(30)]
                          + [f"e:\\perforce\\game\\code\\util\\u{i}.cpp" for i in range(30)]
                          + ["f:\\vc\\crt\\mem.c"])
        b.build()  # initialises root + case dictionary
        rel, ext = b.canon("e:\\perforce\\game\\code\\actions\\dothing.cpp")
        self.assertEqual(rel, "Actions/DoThing.cpp")
        self.assertFalse(ext)
        rel, ext = b.canon("f:\\vc\\crt\\mem.c")
        self.assertTrue(ext)
        self.assertTrue(rel.startswith("_external/"))


class Planning(unittest.TestCase):
    def build(self):
        b = ProjectBuilder()
        b.add_symbols(parse_symbol_table(SAMPLE_TSV))
        b.add_type("Foo", ["e:\\src\\foo.h", "e:\\src\\foo.cpp"], FOO_TEXT)
        return b.build()

    def test_members_without_line_info_go_next_to_their_type(self):
        plan = self.build()
        foo_cpp = plan.files["foo.cpp"]
        self.assertEqual([f.name for f in foo_cpp.functions], ["Foo::Bar"])
        self.assertEqual([v.name for v in foo_cpp.variables], ["Foo::s_count"])
        self.assertEqual([n for n, _ in plan.files["foo.h"].types], ["Foo"])

    def test_file_statics_and_functions_follow_the_compiland(self):
        plan = self.build()
        game = plan.files["game.cpp"]
        self.assertEqual({f.name for f in game.functions}, {"Game::Update", "helper"})
        self.assertEqual([v.name for v in game.variables], ["s_cache"])

    def test_publics_are_kept_apart(self):
        plan = self.build()
        self.assertEqual({p.name for p in plan.publics}, {"?Baz@@YAXXZ", "?g_hidden@@3HA"})

    def test_written_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            stats = write_project(self.build(), tmp)
            out = Path(tmp)
            self.assertGreaterEqual(stats["functions"], 3)
            cpp = (out / "game.cpp").read_text(encoding="utf-8")
            self.assertIn("void Game::Update(int32_t dt)", cpp)
            self.assertIn("// RVA 0x00001000, 64 bytes, line 42", cpp)
            self.assertIn("static int32_t s_cache;", cpp)
            foo_cpp = (out / "foo.cpp").read_text(encoding="utf-8")
            self.assertIn('#include "foo.h"', foo_cpp)
            self.assertTrue((out / "foo.h").read_text(encoding="utf-8").startswith("// foo.h"))
            self.assertIn("#pragma once", (out / "foo.h").read_text(encoding="utf-8"))
            csv_text = (out / "_meta" / "function_map.csv").read_text(encoding="utf-8")
            self.assertNotIn("\r", csv_text)
            self.assertIn("0x00001000,64,Game::Update,game.cpp,42", csv_text)
            self.assertIn("void __cdecl Baz(void)", (out / "_meta" / "publics.txt").read_text())

    def test_template_instances_become_comments(self):
        tsv = ("function\tNs::Make<int>\t\t0x10\t1\t0x10\t8\t\tx.obj\te:\\a\\x.cpp\t7\t__cdecl\tint\tint v\t\n")
        b = ProjectBuilder()
        b.add_symbols(parse_symbol_table(tsv))
        plan = b.build()
        with tempfile.TemporaryDirectory() as tmp:
            write_project(plan, tmp)
            text = next(Path(tmp).rglob("x.cpp")).read_text(encoding="utf-8")
        self.assertIn("// template instantiation", text)
        self.assertNotIn("TODO", text)

    def test_anonymous_namespace_is_wrapped(self):
        tsv = ("function\t`anonymous namespace'::helper\t\t0x10\t1\t0x10\t8\t\tx.obj\te:\\a\\x.cpp\t7\t__cdecl\tvoid\t\t\n")
        b = ProjectBuilder()
        b.add_symbols(parse_symbol_table(tsv))
        with tempfile.TemporaryDirectory() as tmp:
            write_project(b.build(), tmp)
            text = next(Path(tmp).rglob("x.cpp")).read_text(encoding="utf-8")
        self.assertIn("namespace\n{", text)
        self.assertIn("void helper()", text)
        self.assertNotIn("anonymous namespace'::", text)


class ExeOnly(unittest.TestCase):
    def test_analysis_loader(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = ExeAnalysisData(make_analysis_dir(Path(tmp)))
            self.assertEqual(set(d.classes), {"Action", "DoThing", "Thing"})
            self.assertEqual(d.classes["DoThing"].bases, ["Action"])
            self.assertEqual(d.classes["DoThing"].ctor_rvas, [0x3100, 0x3200])
            self.assertEqual(d.classes["DoThing"].dtor_rva, 0x3300)
            self.assertEqual(d.pdb.guid, "3D6B1077-399B-4CD2-9A3E-8CD6F51161E6")
            self.assertEqual(d.pdb.age, 2)
            self.assertTrue(d.pdb.matches("3d6b1077399b4cd29a3e8cd6f51161e6"))

    def test_exe_only_project(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = ExeAnalysisData(make_analysis_dir(Path(tmp)))
            b = ProjectBuilder(exe=exe)
            self.assertEqual(b.add_exe_functions(min_confidence=1), 2)  # unnamed one skipped
            plan = b.build()
            out = Path(tmp) / "out"
            write_project(plan, out)
            # Different spellings of one path were merged and the best case chosen.
            self.assertTrue((out / "Actions" / "DoThing.h").is_file())
            self.assertTrue((out / "Actions" / "DoThing.cpp").is_file())
            header = (out / "Actions" / "DoThing.h").read_text(encoding="utf-8")
            self.assertIn("class DoThing : public Action", header)
            self.assertIn("constructor candidate 1: RVA 0x00003200", header)
            self.assertIn("destructor: RVA 0x00003300", header)
            cpp = (out / "Actions" / "DoThing.cpp").read_text(encoding="utf-8")
            self.assertIn("DoThing::scalar_deleting_dtor", cpp)
            self.assertIn("signature unknown", cpp)

    def test_pdb_exe_match(self):
        ident = PdbIdentity("x.pdb", "3D6B1077-399B-4CD2-9A3E-8CD6F51161E6", 2)
        ok, _ = check_pdb_matches_exe({"guid": "3D6B1077-399B-4CD2-9A3E-8CD6F51161E6", "age": "2"}, ident)
        self.assertTrue(ok)
        ok, why = check_pdb_matches_exe({"guid": "00000000-0000-0000-0000-000000000000"}, ident)
        self.assertFalse(ok)
        self.assertIn("does not match", why)
        ok, _ = check_pdb_matches_exe({}, ident)
        self.assertFalse(ok)

    def test_names(self):
        entries = collect_names(parse_symbol_table(SAMPLE_TSV))
        by_rva = {e["rva"]: e for e in entries}
        self.assertEqual(by_rva[0x1000]["safe"], "Game_Update")
        self.assertEqual(by_rva[0x3000]["name"], "void __cdecl Baz(void)")  # undecorated public
        self.assertEqual(by_rva[0x2000]["kind"], "global")
        self.assertEqual(len({e["safe"] for e in entries}), len(entries))  # unique


class FakeClient:
    """Minimal stand-in for PdbClient (the real one needs the Windows DLL)."""

    def __init__(self, guid="3D6B1077-399B-4CD2-9A3E-8CD6F51161E6"):
        self.guid = guid
        self.opened = False
        self.closed = False
        self.exe_loaded = None
        self.globals_cfg = None

    def open(self, pdb): self.opened = True
    def close(self): self.closed = True
    def pdb_info(self): return {"guid": self.guid, "age": "2"}
    def set_globals_config(self, **o): self.globals_cfg = o
    def dump_symbol_table(self, *a, **k): return SAMPLE_TSV
    def symbol_table(self, *a, **k): return parse_symbol_table(SAMPLE_TSV)
    def enumerate_source_files(self): return "e:\\src\\foo.h\ne:\\src\\foo.cpp\ne:\\src\\game.cpp\n"
    def enumerate_symbols(self, top_level_only=True): return "Foo\nBroken\n"
    def dump_type(self, name, cs=False):
        if name == "Broken":
            raise RuntimeError("boom")
        return FOO_TEXT
    def get_source_files(self, name, cs=False): return "e:\\src\\foo.h\ne:\\src\\foo.cpp\n"


class Pipeline(unittest.TestCase):
    def run_restore(self, client, **kw):
        import restore_project as rp
        with tempfile.TemporaryDirectory() as tmp:
            opts = rp.RestoreOptions(out_dir=Path(tmp) / "out", pdb="x.pdb", verbose=False, **kw)
            stats = rp.restore(client, opts)
            files = sorted(str(p.relative_to(opts.out_dir)).replace("\\", "/")
                           for p in opts.out_dir.rglob("*") if p.is_file())
            return stats, files, opts

    def test_pdb_pipeline_survives_a_broken_type(self):
        client = FakeClient()
        stats, files, opts = self.run_restore(client)
        self.assertTrue(client.opened and client.closed)
        self.assertEqual(stats["types"], 1)
        self.assertIn("foo.h", files)
        self.assertIn("game.cpp", files)
        self.assertIn("_meta/symbols.tsv", files)
        self.assertTrue(any("skipped Broken" in m for m in opts.log))
        self.assertTrue(client.globals_cfg["includeMembers"])

    def test_names_warn_without_exe(self):
        _, _, opts = self.run_restore(FakeClient())
        self.assertTrue(any("WARNING" in m and "--exe" in m for m in opts.log))

    def test_mismatching_pdb_blocks_names_and_strict_aborts(self):
        with tempfile.TemporaryDirectory() as tmp:
            analysis = make_analysis_dir(Path(tmp))
            import restore_project as rp
            opts = rp.RestoreOptions(out_dir=Path(tmp) / "o1", pdb="x.pdb",
                                     exe_analysis_dir=str(analysis), verbose=False)
            rp.restore(FakeClient(guid="11111111-2222-3333-4444-555555555555"), opts)
            self.assertFalse((Path(tmp) / "o1" / "_meta" / "pdb_names_ida.py").exists())
            self.assertTrue(any("does not match" in m for m in opts.log))

            opts = rp.RestoreOptions(out_dir=Path(tmp) / "o2", pdb="x.pdb", check_match="strict",
                                     exe_analysis_dir=str(analysis), verbose=False)
            with self.assertRaises(SystemExit):
                rp.restore(FakeClient(guid="11111111-2222-3333-4444-555555555555"), opts)

            opts = rp.RestoreOptions(out_dir=Path(tmp) / "o3", pdb="x.pdb",
                                     exe_analysis_dir=str(analysis), verbose=False)
            rp.restore(FakeClient(), opts)
            self.assertTrue((Path(tmp) / "o3" / "_meta" / "pdb_names_ida.py").exists())

    def test_exe_analysis_only_needs_no_client(self):
        with tempfile.TemporaryDirectory() as tmp:
            analysis = make_analysis_dir(Path(tmp))
            import restore_project as rp
            opts = rp.RestoreOptions(out_dir=Path(tmp) / "out", exe_analysis_dir=str(analysis),
                                     verbose=False)
            res = analysis / "resources"
            (res / "icons").mkdir(parents=True)
            (res / "resources.rc").write_text("// rc", encoding="utf-8")
            (res / "icons" / "icon_1.ico").write_bytes(b"\0\0\1\0")
            stats = rp.restore(None, opts)
            self.assertGreater(stats["types"], 0)
            # resources found by the exe analysis are carried into the restored tree
            self.assertTrue((Path(tmp) / "out" / "Resources" / "resources.rc").is_file())
            self.assertTrue((Path(tmp) / "out" / "Resources" / "icons" / "icon_1.ico").is_file())


class ClientWrappers(unittest.TestCase):
    """Drive PdbClient against a fake DLL that follows the two-phase buffer protocol."""

    @staticmethod
    def two_phase(text, code=0):
        def fn(*args):
            buf, size, req = args[-3], args[-2], args[-1]
            if code != 0:
                return code
            req._obj.value = len(text) + 1
            if buf is not None:
                ctypes.memmove(buf, ctypes.create_unicode_buffer(text),
                               (len(text) + 1) * ctypes.sizeof(ctypes.c_wchar))
            return 0
        return fn

    def client(self, **fns):
        c = PdbClient.__new__(PdbClient)
        dll = SimpleNamespace(**fns)
        c.api = SimpleNamespace(dll=dll)
        return c

    def test_symbol_table_and_pdb_info(self):
        c = self.client(PdbApi_DumpSymbolTable=self.two_phase(SAMPLE_TSV),
                        PdbApi_GetPdbInfo=self.two_phase("guid=AA\nage=3\nplatform=0x3\n"))
        self.assertEqual(len(c.symbol_table()), 12)
        self.assertEqual(c.pdb_info(), {"guid": "AA", "age": "3", "platform": "0x3"})

    def test_error_carries_native_message(self):
        c = self.client(PdbApi_DumpGlobalFunctions=self.two_phase("", code=-1),
                        PdbApi_GetLastError=self.two_phase("DIA session not initialized"))
        with self.assertRaises(PdbApiError) as cm:
            c.dump_functions("x")
        self.assertEqual(cm.exception.code, -1)
        self.assertIn("NOT_INITIALIZED", str(cm.exception))
        self.assertIn("DIA session not initialized", str(cm.exception))
        self.assertIsInstance(cm.exception, RuntimeError)  # old `except RuntimeError` still works

    def test_globals_config_roundtrip(self):
        state = PdbApiGlobalsConfig(1, 1, 1, 1, 0, 0, 0, 0)

        def get(p):
            p._obj.showAddress = state.showAddress
            for f, _ in PdbApiGlobalsConfig._fields_:
                setattr(p._obj, f, getattr(state, f))
            return 0

        def set_(p):
            for f, _ in PdbApiGlobalsConfig._fields_:
                setattr(state, f, getattr(p._obj, f))
            return 0

        c = self.client(PdbApi_GetGlobalsConfig=get, PdbApi_SetGlobalsConfig=set_)
        c.set_globals_config(includeMembers=True, sortByAddress=True)
        self.assertEqual((state.includeMembers, state.sortByAddress, state.showAddress), (1, 1, 1))
        with self.assertRaises(ValueError):
            c.set_globals_config(bogus=1)

    def test_struct_layout_matches_c_header(self):
        self.assertEqual(ctypes.sizeof(PdbApiGlobalsConfig), 8 * 4)


if __name__ == "__main__":
    unittest.main()
