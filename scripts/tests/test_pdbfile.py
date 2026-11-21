"""Tests of the pure-Python PDB reader on synthetic data (no real PDB needed)."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dumppdb_tools.pdbfile.info import parse_info
from dumppdb_tools.pdbfile.msf import MAGIC, Msf, PdbFormatError
from dumppdb_tools.pdbfile.types import (
    LF_ARGLIST, LF_CLASS, LF_FIELDLIST, LF_MEMBER, LF_ONEMETHOD, LF_MFUNCTION, LF_POINTER,
    LF_PROCEDURE, LF_MODIFIER, LF_ARRAY, TypeRenderer, TypeTable, read_numeric,
)

BLOCK = 512


def build_msf(streams: list[bytes]) -> bytes:
    """Smallest valid MSF 7.00 file holding the given streams."""
    blocks: list[bytes] = [b"", b"", b""]          # 0: header, 1-2: free block maps
    stream_blocks: list[list[int]] = []
    for data in streams:
        idx = []
        for off in range(0, len(data), BLOCK):
            idx.append(len(blocks))
            blocks.append(data[off:off + BLOCK].ljust(BLOCK, b"\0"))
        stream_blocks.append(idx)

    directory = struct.pack("<I", len(streams)) + b"".join(struct.pack("<I", len(d)) for d in streams)
    for idx in stream_blocks:
        directory += b"".join(struct.pack("<I", i) for i in idx)
    dir_blocks = []
    for off in range(0, len(directory), BLOCK):
        dir_blocks.append(len(blocks))
        blocks.append(directory[off:off + BLOCK].ljust(BLOCK, b"\0"))
    map_block = len(blocks)
    blocks.append(b"".join(struct.pack("<I", i) for i in dir_blocks).ljust(BLOCK, b"\0"))

    header = MAGIC + struct.pack("<IIIIII", BLOCK, 1, len(blocks), len(directory), 0, map_block)
    blocks[0] = header.ljust(BLOCK, b"\0")
    blocks[1] = blocks[2] = b"\0" * BLOCK
    return b"".join(blocks)


def rec(kind: int, payload: bytes) -> bytes:
    body = struct.pack("<H", kind) + payload
    body += b"\0" * ((-len(body)) % 4)
    return struct.pack("<H", len(body)) + body


def cs(text: str) -> bytes:
    return text.encode() + b"\0"


def tpi_stream(records: list[bytes]) -> bytes:
    body = b"".join(records)
    header = struct.pack("<IIIIIHHIIiiiiii", 20040203, 56, 0x1000, 0x1000 + len(records), len(body),
                         0xFFFF, 0xFFFF, 4, 0x3FFFF, 0, 0, 0, 0, 0, 0)
    return header + body


T_INT4, T_VOID, T_UCHAR = 0x74, 0x03, 0x20


class Container(unittest.TestCase):
    def test_streams_round_trip_including_multi_block_ones(self):
        streams = [b"zero", b"A" * 1300, b"", b"B" * 10]
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "x.pdb"
            p.write_bytes(build_msf(streams))
            m = Msf(p)
            self.assertEqual(m.stream_count, 4)
            for i, s in enumerate(streams):
                self.assertEqual(m.stream(i), s)
            self.assertEqual(m.stream(99), b"")

    def test_rejects_other_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "x.pdb"
            p.write_bytes(b"not a pdb" * 100)
            with self.assertRaises(PdbFormatError):
                Msf(p)

    def test_info_stream_has_guid_and_age(self):
        guid = bytes.fromhex("77106b3d9b39d24c9a3e8cd6f51161e6")
        info = struct.pack("<III", 20000404, 0x535D9551, 2) + guid
        info += struct.pack("<I", 0) + struct.pack("<II", 0, 1) + struct.pack("<I", 1) + struct.pack("<I", 0)
        info += struct.pack("<I", 0)
        parsed = parse_info(info)
        self.assertEqual(parsed.guid, "3D6B1077-399B-4CD2-9A3E-8CD6F51161E6")
        self.assertEqual(parsed.age, 2)


class Numerics(unittest.TestCase):
    def test_numeric_leaf_forms(self):
        self.assertEqual(read_numeric(struct.pack("<H", 42), 0), (42, 2))
        self.assertEqual(read_numeric(struct.pack("<Hb", 0x8000, -5), 0), (-5, 3))
        self.assertEqual(read_numeric(struct.pack("<HH", 0x8002, 65000), 0), (65000, 4))
        self.assertEqual(read_numeric(struct.pack("<Hi", 0x8003, -100000), 0), (-100000, 6))
        self.assertEqual(read_numeric(struct.pack("<HQ", 0x800A, 2**40), 0), (2**40, 10))


class Types(unittest.TestCase):
    def render(self, records, ti, name="", style="cstdint"):
        tt = TypeTable(tpi_stream(records), style)
        return TypeRenderer(tt, style).declare(0x1000 + ti, name)

    def test_primitives_and_int_styles(self):
        tt = TypeTable(tpi_stream([]))
        self.assertEqual(TypeRenderer(tt, "cstdint").declare(T_INT4, "x"), "int32_t x")
        self.assertEqual(TypeRenderer(tt, "msvc").declare(T_INT4, "x"), "int x")
        self.assertEqual(TypeRenderer(tt, "native").declare(T_UCHAR, ""), "unsigned char")

    def test_pointers_const_and_arrays(self):
        records = [
            rec(LF_POINTER, struct.pack("<II", T_INT4, 0x0A | (4 << 13))),          # 0: int*
            rec(LF_POINTER, struct.pack("<II", 0x1000, 0x0A | (4 << 13))),          # 1: int**
            rec(LF_MODIFIER, struct.pack("<IH", T_INT4, 1)),                         # 2: const int
            rec(LF_POINTER, struct.pack("<II", 0x1002, 0x0A | (4 << 13))),          # 3: const int*
            rec(LF_ARRAY, struct.pack("<II", T_INT4, 0x75) + struct.pack("<H", 40) + cs("")),  # 4: int[10]
            rec(LF_POINTER, struct.pack("<II", 0x1004, 0x0A | (4 << 13))),          # 5: int (*)[10]
        ]
        self.assertEqual(self.render(records, 0, "p"), "int32_t* p")
        self.assertEqual(self.render(records, 1, "pp"), "int32_t** pp")
        self.assertEqual(self.render(records, 3, "c"), "const int32_t* c")
        self.assertEqual(self.render(records, 4, "a"), "int32_t a[10]")
        self.assertEqual(self.render(records, 5, "pa"), "int32_t (*pa)[10]")

    def test_function_pointer(self):
        records = [
            rec(LF_ARGLIST, struct.pack("<II", 2, T_INT4) + struct.pack("<I", T_UCHAR)),   # 0
            rec(LF_PROCEDURE, struct.pack("<IBBHI", T_VOID, 0, 0, 2, 0x1000)),              # 1: void(int,uchar)
            rec(LF_POINTER, struct.pack("<II", 0x1001, 0x0A | (4 << 13))),                  # 2
        ]
        self.assertEqual(self.render(records, 2, "cb"), "void (*cb)(int32_t, unsigned char)")

    def test_class_declaration_with_base_members_and_methods(self):
        fl_base = rec(LF_FIELDLIST, struct.pack("<HI", 3, T_INT4)[:0])  # placeholder, replaced below
        records = []
        # 0: arglist(int)   1: mfunction void(int) const-less   2: fieldlist   3: class Foo
        records.append(rec(LF_ARGLIST, struct.pack("<II", 1, T_INT4)))
        records.append(rec(LF_MFUNCTION, struct.pack("<IIIBBHIi", T_VOID, 0x1003, 0x1004, 11, 0, 1, 0x1000, 0)))
        fields = (struct.pack("<HHI", LF_MEMBER, 3, T_INT4) + struct.pack("<H", 8) + cs("m_value")
                  + struct.pack("<HHI", LF_ONEMETHOD, (3 | (1 << 2)), 0x1001) + cs("Set"))
        records.append(rec(LF_FIELDLIST, fields))
        records.append(rec(LF_CLASS, struct.pack("<HHIII", 2, 0, 0x1002, 0, 0) + struct.pack("<H", 16) + cs("Game::Foo")))
        tt = TypeTable(tpi_stream(records))
        r = TypeRenderer(tt)
        text = r.udt_declaration(0x1003, display_name="Foo")
        self.assertIn("class Foo  // size 0x10", text)
        self.assertIn("public:", text)
        self.assertIn("int32_t m_value;  // +0x8", text)
        self.assertIn("virtual void Set(int32_t);", text)


if __name__ == "__main__":
    unittest.main()
