"""SQLite database for source path storage and type-source resolution."""

import sqlite3
from pathlib import Path

from dumppdb_tools.database.schema import SCHEMA
from dumppdb_tools.models import SourcePath
from dumppdb_tools.recovery.path_normalizer import normalize_path


class SourceDatabase:
    """Manages the source-path SQLite database.

    The database stores:

    * ``normalized_paths`` — unique normalized paths (``id``, ``path``).
    * ``original_paths`` — original paths with a foreign key to
      ``normalized_paths`` and a normalized extension (``id``, ``path``,
      ``normalized_id``, ``extension``).
    * ``settings`` — singleton key-value store (e.g. ``output_extension``,
      ``path_prefix``).
    * ``types`` — types discovered from the PDB (``id``, ``name``).
    * ``type_relations`` — type-to-type relations (``template_instance``,
      ``meta_variant``).
    * ``type_source_files`` — type -> source-file links with score and
      user-preferred flag.
    """

    def __init__(self, db_path: str | Path):
        self.db_path = str(db_path)
        self.conn: sqlite3.Connection | None = None

    def open(self) -> None:
        """Open the database, create tables, and run any migrations."""
        self.conn = sqlite3.connect(self.db_path)
        self.conn.executescript(SCHEMA)
        self._migrate()

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

    def _migrate(self) -> None:
        """Apply column migrations for databases created by older versions."""
        assert self.conn is not None

        columns = {
            row[1]
            for row in self.conn.execute(
                "PRAGMA table_info(type_source_files)"
            ).fetchall()
        }

        if "is_external" not in columns:
            self.conn.execute(
                "ALTER TABLE type_source_files "
                "ADD COLUMN is_external INTEGER NOT NULL DEFAULT 0"
            )

    def commit(self) -> None:
        """Commit the current transaction."""
        assert self.conn is not None
        self.conn.commit()

    def validate_for_resolution(self) -> bool:
        """Check if the DB is ready for type-source resolution.

        Returns ``True`` only if the DB file exists, the
        ``normalized_paths`` table is present and non-empty.
        This check does NOT auto-create tables.
        """
        if not Path(self.db_path).exists():
            return False

        conn = sqlite3.connect(self.db_path)

        try:
            row = conn.execute(
                "SELECT name FROM sqlite_master "
                "WHERE type='table' AND name='normalized_paths'"
            ).fetchone()

            if row is None:
                return False

            count = conn.execute(
                "SELECT COUNT(*) FROM normalized_paths"
            ).fetchone()

            return count is not None and count[0] > 0
        except sqlite3.Error:
            return False
        finally:
            conn.close()

    # ------------------------------------------------------------------
    # Settings (singleton key-value)
    # ------------------------------------------------------------------

    def set_setting(self, key: str, value: str) -> None:
        """Set a singleton setting value."""
        assert self.conn is not None
        self.conn.execute(
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)",
            (key, value),
        )
        self.conn.commit()

    def get_setting(self, key: str) -> str | None:
        """Get a singleton setting value, or ``None`` if not set."""
        assert self.conn is not None
        row = self.conn.execute(
            "SELECT value FROM settings WHERE key = ?",
            (key,),
        ).fetchone()
        return row[0] if row else None

    # ------------------------------------------------------------------
    # Types
    # ------------------------------------------------------------------

    def upsert_type(self, name: str) -> int:
        """Insert a type if it doesn't exist, return its id."""
        assert self.conn is not None
        self.conn.execute(
            "INSERT OR IGNORE INTO types (name) VALUES (?)",
            (name,),
        )
        row = self.conn.execute(
            "SELECT id FROM types WHERE name = ?",
            (name,),
        ).fetchone()
        return row[0]

    def get_type_id(self, name: str) -> int | None:
        """Return the id of a type by name, or ``None`` if not found."""
        assert self.conn is not None
        row = self.conn.execute(
            "SELECT id FROM types WHERE name = ?",
            (name,),
        ).fetchone()
        return row[0] if row else None

    def get_all_types(self) -> list[tuple[int, str]]:
        """Return all types as ``(id, name)`` tuples."""
        assert self.conn is not None
        return self.conn.execute("SELECT id, name FROM types").fetchall()

    # ------------------------------------------------------------------
    # Type relations
    # ------------------------------------------------------------------

    def add_type_relation(
        self,
        type_id: int,
        related_type_id: int,
        relation_type: str,
    ) -> None:
        """Insert a type-to-type relation (idempotent)."""
        assert self.conn is not None
        self.conn.execute(
            "INSERT OR IGNORE INTO type_relations "
            "(type_id, related_type_id, relation_type) VALUES (?, ?, ?)",
            (type_id, related_type_id, relation_type),
        )

    # ------------------------------------------------------------------
    # Normalized paths
    # ------------------------------------------------------------------

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

    def find_normalized_id(self, path: str) -> int | None:
        """Find a normalized path id by case-insensitive match.

        Returns ``None`` if no match is found.
        """
        assert self.conn is not None
        row = self.conn.execute(
            "SELECT id FROM normalized_paths WHERE lower(path) = lower(?)",
            (path,),
        ).fetchone()
        return row[0] if row else None

    def insert_normalized_path(self, path: str) -> int:
        """Insert a normalized path and return its id."""
        return self._get_normalized_id(path)

    # ------------------------------------------------------------------
    # Type-source-file links
    # ------------------------------------------------------------------

    def add_type_source_file(
        self,
        type_id: int,
        source_file_id: int,
        score: int = 0,
        user_preferred: bool = False,
        is_external: bool = False,
    ) -> None:
        """Insert or update a type -> source-file link.

        *is_external* marks links produced by the global (all-sources)
        search when the type's own sources never reached ``MIN_CONFIDENCE``.
        """
        assert self.conn is not None
        self.conn.execute(
            "INSERT OR REPLACE INTO type_source_files "
            "(type_id, source_file_id, score, user_preferred, is_external) "
            "VALUES (?, ?, ?, ?, ?)",
            (
                type_id,
                source_file_id,
                score,
                int(user_preferred),
                int(is_external),
            ),
        )

    # ------------------------------------------------------------------
    # Original SourcePath insertion (existing API)
    # ------------------------------------------------------------------

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

    # ------------------------------------------------------------------
    # Path resolution helpers
    # ------------------------------------------------------------------

    def resolve_path_id(
        self,
        raw_path: str,
        path_prefix: str,
        cache: dict[str, int] | None = None,
    ) -> int | None:
        """Trim *raw_path* and return its ``normalized_paths`` id.

        Applies :func:`~dumppdb_tools.recovery.trim_to_normalized` to
        convert the raw PDB path to the DB format, then looks it up by
        case-insensitive match (inserting it if absent).

        *cache* is an optional ``{trimmed: id}`` dict shared across calls
        to avoid redundant DB lookups.
        """
        from dumppdb_tools.recovery import trim_to_normalized

        trimmed = trim_to_normalized(raw_path, path_prefix)
        if trimmed is None:
            return None

        if cache is not None and trimmed in cache:
            return cache[trimmed]

        found_id = self.find_normalized_id(trimmed)
        if found_id is None:
            found_id = self.insert_normalized_path(trimmed)

        if cache is not None:
            cache[trimmed] = found_id

        return found_id