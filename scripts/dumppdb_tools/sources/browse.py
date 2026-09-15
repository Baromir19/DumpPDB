"""Locate type definitions inside the Visual Studio Browse.VC.db database.

These helpers are shared by the ``compare_type_sources`` (cross-checking the
recorded paths against the browse database) and ``reconstruct_sources``
(skipping types that are already defined there) CLI tools.
"""

import sqlite3
from dataclasses import dataclass
from enum import IntFlag
from pathlib import Path

from dumppdb_tools.sources.type_sources import TypeSource


class CodeItemAttributes(IntFlag):
    NONE = 0
    DECLARATION = 1
    DEFINITION = 2


@dataclass
class BrowseDefinition:
    """A *definition* of a type located inside the Browse.VC.db database."""

    type_name: str
    kind: str
    file_id: int | None
    code_item_id: int


# Kinds we care about when looking for the actual type definition.
DEFINITION_KINDS = {"enum", "union", "struct", "class", "interface"}

# Flag a code_item must carry to be treated as the definition.
DEFINITION_ATTRIBUTE = CodeItemAttributes.DEFINITION


def open_browse_db(browse_db_path: str | Path) -> sqlite3.Connection:
    """Open the VS browse database with a dict-like row factory."""
    conn = sqlite3.connect(browse_db_path)
    conn.row_factory = sqlite3.Row
    return conn


def load_kinds(conn) -> dict[int, str]:
    """Snapshot ``code_item_kinds`` as ``{id: name}``."""
    return {
        row["id"]: row["name"]
        for row in conn.execute("SELECT id, name FROM code_item_kinds").fetchall()
    }


def lookup_file_name(conn, file_id) -> str | None:
    """Resolve a ``files.id`` to its ``name`` (compared case-insensitively)."""
    row = conn.execute(
        "SELECT name FROM files WHERE id = ?",
        (file_id,),
    ).fetchone()

    return row["name"] if row else None


def find_definitions_in_browse(
    conn, items: list[TypeSource]
) -> dict[str, BrowseDefinition]:
    """Find the relevant-kind DEFINITION of every *item* inside the browse DB.

    For each type name we scan its ``code_items`` rows and keep the first one
    whose ``kind`` name is one of ``DEFINITION_KINDS`` and whose ``attributes``
    carry the ``DEFINITION`` flag.
    """
    kinds = load_kinds(conn)

    definitions: dict[str, BrowseDefinition] = {}

    for item in items:
        type_name = item.type_name

        rows = conn.execute(
            """
            SELECT ci.id AS code_item_id,
                   ci.file_id,
                   ci.kind,
                   ci.attributes
            FROM code_items ci
            WHERE ci.name = ? COLLATE NOCASE
            """,
            (type_name,),
        ).fetchall()

        for row in rows:
            kind_name = kinds.get(row["kind"])

            if kind_name not in DEFINITION_KINDS:
                continue

            if not (row["attributes"] & DEFINITION_ATTRIBUTE):
                continue

            definitions[type_name] = BrowseDefinition(
                type_name=type_name,
                kind=kind_name,
                file_id=row["file_id"],
                code_item_id=row["code_item_id"],
            )
            break

    return definitions


def locate_browse_definitions(
    browse_db_path: str | Path,
    items: list[TypeSource],
) -> dict[str, BrowseDefinition]:
    """Open *browse_db_path* and return the located definitions for *items*."""
    conn = open_browse_db(browse_db_path)
    try:
        return find_definitions_in_browse(conn, items)
    finally:
        conn.close()