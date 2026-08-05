"""SQLite connection wrapper.

Provides a single :class:`Database` object that owns the connection and
exposes a thin cursor API.  WAL mode is enabled so concurrent readers
don't block the writer.
"""

import sqlite3
from pathlib import Path


class Database:
    """Owns the SQLite connection lifecycle."""

    def __init__(self, db_path: str | Path) -> None:
        self._path = Path(db_path)
        self._con: sqlite3.Connection | None = None

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def connect(self) -> None:
        """Open (or create) the database file and configure pragmas."""
        self._con = sqlite3.connect(self._path)
        self._con.row_factory = sqlite3.Row

        cur = self._con.cursor()
        # WAL gives us concurrent reads without blocking writes
        cur.execute("PRAGMA journal_mode = WAL")
        # Enforce FK constraints on every connection
        cur.execute("PRAGMA foreign_keys = ON")
        self._con.commit()

    def close(self) -> None:
        """Commit pending work and close the connection."""
        if self._con is not None:
            self._con.commit()
            self._con.close()
            self._con = None

    def __enter__(self) -> "Database":
        self.connect()
        return self

    def __exit__(self, *_) -> None:
        self.close()

    # ------------------------------------------------------------------
    # Internal helpers used by repository / models
    # ------------------------------------------------------------------

    @property
    def connection(self) -> sqlite3.Connection:
        if self._con is None:
            raise RuntimeError(
                "Database is not connected. Call connect() first."
            )
        return self._con

    def cursor(self) -> sqlite3.Cursor:
        return self.connection.cursor()

    def execute(self, sql: str, params: tuple = ()) -> sqlite3.Cursor:
        """Execute a single SQL statement and return its cursor."""
        return self.connection.execute(sql, params)

    def executemany(self, sql: str, params_seq) -> sqlite3.Cursor:
        return self.connection.executemany(sql, params_seq)

    def commit(self) -> None:
        self.connection.commit()
