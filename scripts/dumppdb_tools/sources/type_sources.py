"""Type-source records loaded from the DumpPDB SQLite database.

A :class:`TypeSource` bundles a type's name with the single source path that
the resolution pipeline recorded for it. The loader used by the CLI tools
(``compare_type_sources`` and ``reconstruct_sources``) lives here so the
persisted format is read in exactly one place.
"""

from dataclasses import dataclass
from pathlib import Path
import sqlite3


@dataclass(slots=True)
class TypeSource:
    """A type persisted in the DumpPDB database and its recorded source.

    Attributes:
        type_name:    The type's name as stored in ``types.name``.
        related_name: Name of the owning type when this type is the target of
            a relation (meta variant / template instantiation), else ``None``.
        path:         Extension-stripped, normalized relative path of the
            recorded source file, or ``None`` when nothing was resolved.
    """

    type_name: str
    related_name: str | None
    path: str | None


def load_type_sources(db_path: str | Path) -> list[TypeSource]:
    """Load the enabled, non-related :class:`TypeSource` records from *db_path*.

    Mirrors the query used by the resolution pipeline: types that appear as the
    *related* side of a relation are handled through their owning type, so they
    are skipped here. For the remaining types the best recorded source file is
    chosen (user-preferred links first, then by score).
    """
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    try:
        types = conn.execute(
            """
            SELECT id, name
            FROM types
            WHERE disabled = 0
            """
        ).fetchall()

        result: list[TypeSource] = []

        for type_row in types:
            type_id = type_row["id"]
            type_name = type_row["name"]

            is_related = conn.execute(
                """
                SELECT 1
                FROM type_relations
                WHERE related_type_id = ?
                LIMIT 1
                """,
                (type_id,),
            ).fetchone()

            if is_related:
                continue

            relation = conn.execute(
                """
                SELECT related_type_id
                FROM type_relations
                WHERE type_id = ?
                ORDER BY related_type_id
                LIMIT 1
                """,
                (type_id,),
            ).fetchone()

            related_name = None

            if relation:
                related_row = conn.execute(
                    """
                    SELECT name
                    FROM types
                    WHERE id = ?
                    """,
                    (relation["related_type_id"],),
                ).fetchone()

                if related_row:
                    related_name = related_row["name"]

            source = conn.execute(
                """
                SELECT
                    tsf.source_file_id,
                    np.path
                FROM type_source_files tsf
                JOIN normalized_paths np
                    ON np.id = tsf.source_file_id
                WHERE tsf.type_id = ?
                ORDER BY
                    tsf.user_preferred DESC,
                    tsf.score DESC,
                    tsf.source_file_id ASC
                LIMIT 1
                """,
                (type_id,),
            ).fetchone()

            path = source["path"] if source else None

            result.append(
                TypeSource(
                    type_name=type_name,
                    related_name=related_name,
                    path=path,
                )
            )

        return result

    finally:
        conn.close()