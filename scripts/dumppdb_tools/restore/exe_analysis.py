"""Loader for the directory written by ``-exeanalyze`` / ``ExeApi_ExportAnalysis``.

It exposes the parts the restoration code needs: the class -> source file map
recovered from assert strings, per-function source/class attribution, every
spelling of every embedded source path (for case recovery) and the identity of
the PDB the executable was linked against.
"""

from __future__ import annotations

import csv
import json
import re
from dataclasses import dataclass, field
from pathlib import Path


@dataclass(slots=True)
class ExeClass:
    name: str
    type_descriptor_rva: int = 0
    primary_vftable_rva: int = 0
    vftable_count: int = 0
    slot_count: int = 0
    bases: list[str] = field(default_factory=list)
    ctor_rvas: list[int] = field(default_factory=list)
    dtor_rva: int | None = None
    deleting_dtor_rva: int | None = None
    source: str = ""


@dataclass(slots=True)
class ExeFunction:
    rva: int
    size: int = 0
    name: str = ""
    confidence: int = 0
    class_name: str = ""
    source: str = ""
    callers: int = 0


@dataclass(slots=True)
class PdbIdentity:
    path: str = ""
    guid: str = ""
    age: int = 0

    def matches(self, guid: str, age: int | None = None) -> bool:
        """True when a PDB's GUID (and age, when given) equals this identity."""
        if not self.guid or not guid:
            return False
        if self.guid.replace("-", "").upper() != guid.replace("-", "").upper():
            return False
        return age is None or age == self.age


def _hex(text: str) -> int:
    text = text.strip()
    return int(text, 16) if text else 0


def _opt_hex(text: str) -> int | None:
    text = text.strip()
    return int(text, 16) if text else None


class ExeAnalysisData:
    """Read-only view of an exported exe analysis directory."""

    def __init__(self, directory: str | Path):
        self.directory = Path(directory)
        self.classes: dict[str, ExeClass] = {}
        self.functions: dict[int, ExeFunction] = {}
        self.spellings: list[str] = []
        self.pdb = PdbIdentity()
        self.module_info: dict[str, str] = {}
        self._load()

    # -- queries ---------------------------------------------------------------

    def class_source(self, name: str) -> str:
        c = self.classes.get(name)
        return c.source if c else ""

    def function_source(self, rva: int) -> str:
        f = self.functions.get(rva)
        return f.source if f else ""

    def functions_of(self, class_name: str) -> list[ExeFunction]:
        return [f for f in self.functions.values() if f.class_name == class_name]

    # -- loading ---------------------------------------------------------------

    def _path(self, name: str) -> Path:
        return self.directory / name

    def _load(self) -> None:
        if not self.directory.is_dir():
            raise FileNotFoundError(f"exe analysis directory not found: {self.directory}")
        self._load_classes()
        self._load_functions()
        self._load_spellings()
        self._load_summary()

    def _rows(self, name: str):
        p = self._path(name)
        if not p.is_file():
            return
        with p.open(newline="", encoding="utf-8", errors="replace") as fh:
            yield from csv.DictReader(fh)

    def _load_classes(self) -> None:
        for r in self._rows("classes.csv"):
            c = ExeClass(
                name=r["class"],
                type_descriptor_rva=_hex(r.get("type_descriptor_rva", "")),
                primary_vftable_rva=_hex(r.get("primary_vftable_rva", "")),
                vftable_count=int(r.get("vftable_count") or 0),
                slot_count=int(r.get("slot_count") or 0),
                bases=[b for b in (r.get("bases") or "").split(";") if b],
                ctor_rvas=[_hex(x) for x in (r.get("ctor_rvas") or "").split(";") if x],
                dtor_rva=_opt_hex(r.get("dtor_rva", "")),
                deleting_dtor_rva=_opt_hex(r.get("deleting_dtor_rva", "")),
                source=r.get("source", ""),
            )
            self.classes[c.name] = c

    def _load_functions(self) -> None:
        for r in self._rows("functions.csv"):
            rva = _hex(r["rva"])
            self.functions[rva] = ExeFunction(
                rva=rva,
                size=int(r.get("size") or 0),
                name=r.get("name", ""),
                confidence=int(r.get("confidence") or 0),
                class_name=r.get("class", ""),
                source=r.get("source", ""),
                callers=int(r.get("callers") or 0),
            )

    def _load_spellings(self) -> None:
        p = self._path("source_spellings.txt")
        if p.is_file():
            self.spellings = [ln.strip() for ln in p.read_text(
                encoding="utf-8", errors="replace").splitlines() if ln.strip()]

    _KV = re.compile(r"^(?P<k>[A-Za-z][A-Za-z /()]*?)\s*:\s*(?P<v>.+?)\s*$")

    def _load_summary(self) -> None:
        p = self._path("summary.txt")
        if not p.is_file():
            return
        for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
            m = self._KV.match(line)
            if not m:
                continue
            k, v = m.group("k").strip(), m.group("v").strip()
            self.module_info[k] = v
            if k == "PDB path":
                self.pdb.path = v
            elif k == "PDB GUID":
                self.pdb.guid = v
            elif k == "PDB age":
                try:
                    self.pdb.age = int(v)
                except ValueError:
                    pass


def parse_pdb_info(text: str) -> dict[str, str]:
    """Parse the ``key=value`` text of ``PdbApi_GetPdbInfo``."""
    info: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            info[k.strip()] = v.strip()
    return info


def load_names(directory: str | Path) -> list[dict]:
    """``names.json`` written by the exe analysis (address -> name entries)."""
    p = Path(directory) / "names.json"
    return json.loads(p.read_text(encoding="utf-8")) if p.is_file() else []
