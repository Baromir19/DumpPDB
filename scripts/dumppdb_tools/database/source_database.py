"""SQLite database for source path storage."""

import sqlite3
from pathlib import Path

from dumppdb_tools.database.schema import SCHEMA
from dumppdb_tools.models import SourcePath
from dumppdb_tools.recovery.path_normalizer import normalize_path


class SourceDatabase:
    """Manages the source-path SQLite database.

    The database stores two tables:

    * ``normalized_paths`` — unique normalized paths (``id``, ``path``).
    * ``original_paths`` — original paths with a foreign key to
      ``normalized_paths`` and a normalized extension (``id``, ``path``,
      ``normalized_id``, ``extension``).
    """

    def __init__(self, db_path: str | Path):
        self.db_path = str(db_path)
        self.conn: sqlite3.Connection | None = None

    def open(self) -> None:
        """Open the database and create tables if they don't exist."""
        self.conn = sqlite3.connect(self.db_path)
        self.conn.executescript(SCHEMA)

    def close(self) -> None:
        """Close the database connection."""
        if self.conn is not None:
            self.conn.close()
            self.conn = None

    def __enter__(self) -> "SourceDatabase":
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def _get_normalized_id(self, normalized: str) -> int:
        """Insert a normalized path and return its id."""
        assert self.conn is not None

        cursor = self.conn.execute(
            "INSERT OR IGNORE INTO normalized_paths (path) VALUES (?)",
            (normalized,),
        )

        if cursor.rowcount == 0:
            row = self.conn.execute(
                "SELECT id FROM normalized_paths WHERE path = ?",
                (normalized,),
            ).fetchone()
            return row[0]

        return cursor.lastrowid

    def insert(self, source: SourcePath) -> None:
        """Insert a single source path into the database.

        The extension is extracted from the original path and normalized
        (backslashes converted to forward slashes).
        """
        assert self.conn is not None

        normalized_id = self._get_normalized_id(source.normalized)

        extension = Path(source.original).suffix
        extension = normalize_path(extension)

        self.conn.execute(
            "INSERT INTO original_paths (path, normalized_id, extension) "
            "VALUES (?, ?, ?)",
            (source.original, normalized_id, extension),
        )

    def insert_many(self, sources: list[SourcePath]) -> None:
        """Insert multiple source paths in a single transaction."""
        assert self.conn is not None

        self.conn.execute("BEGIN")

        try:
            for source in sources:
                self.insert(source)

            self.conn.commit()
        except Exception:
            self.conn.rollback()
            raise