"""Repository: all database read / write operations.

The single :class:`SymbolRepository` class is the only place in the
codebase that issues SQL.  The importer and the analysis layer both use
it — they never touch the :class:`~dumppdb_tools.storage.database.Database`
object directly.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterator

from dumppdb_tools.storage.database import Database


@dataclass(frozen=True, slots=True)
class CandidateRow:
    symbol_name: str
    file_path: str
    score: int
    reason: str | None


class SymbolRepository:
    """All CRUD operations for the pipeline schema."""

    def __init__(self, db: Database) -> None:
        self._db = db

    # ------------------------------------------------------------------
    # symbols
    # ------------------------------------------------------------------

    def get_or_create_symbol(self, name: str) -> int:
        """Return the id of *name*, inserting it if necessary."""
        self._db.execute(
            "INSERT OR IGNORE INTO symbols(name) VALUES (?)",
            (name,),
        )
        row = self._db.execute(
            "SELECT id FROM symbols WHERE name = ?",
            (name,),
        ).fetchone()
        return row["id"]

    def iter_symbols(self) -> Iterator[tuple[int, str]]:
        """Yield (id, name) for every row in *symbols*."""
        cur = self._db.execute("SELECT id, name FROM symbols ORDER BY name")
        for row in cur:
            yield row["id"], row["name"]

    def symbol_count(self) -> int:
        return self._db.execute(
            "SELECT COUNT(*) FROM symbols"
        ).fetchone()[0]

    # ------------------------------------------------------------------
    # source_files
    # ------------------------------------------------------------------

    def get_or_create_file(self, path: str) -> int:
        """Return the id of *path*, inserting it if necessary."""
        self._db.execute(
            "INSERT OR IGNORE INTO source_files(path) VALUES (?)",
            (path,),
        )
        row = self._db.execute(
            "SELECT id FROM source_files WHERE path = ?",
            (path,),
        ).fetchone()
        return row["id"]

    def iter_files(self) -> Iterator[tuple[int, str]]:
        """Yield (id, path) for every row in *source_files*."""
        cur = self._db.execute(
            "SELECT id, path FROM source_files ORDER BY path"
        )
        for row in cur:
            yield row["id"], row["path"]

    def file_count(self) -> int:
        return self._db.execute(
            "SELECT COUNT(*) FROM source_files"
        ).fetchone()[0]

    # ------------------------------------------------------------------
    # symbol_source_files  (raw DIA links)
    # ------------------------------------------------------------------

    def link_symbol_file(self, symbol_id: int, file_id: int) -> None:
        """Record the DIA link between a symbol and a source file."""
        self._db.execute(
            "INSERT OR IGNORE INTO symbol_source_files(symbol_id, file_id)"
            " VALUES (?, ?)",
            (symbol_id, file_id),
        )

    def get_files_for_symbol(self, symbol_id: int) -> list[tuple[int, str]]:
        """Return [(file_id, path), ...] for all files linked to *symbol_id*."""
        rows = self._db.execute(
            """
            SELECT sf.id, sf.path
            FROM   symbol_source_files ssf
            JOIN   source_files sf ON sf.id = ssf.file_id
            WHERE  ssf.symbol_id = ?
            """,
            (symbol_id,),
        ).fetchall()
        return [(r["id"], r["path"]) for r in rows]

    # ------------------------------------------------------------------
    # file_statistics
    # ------------------------------------------------------------------

    def rebuild_file_statistics(self) -> None:
        """Recompute symbol_count for every file in one pass.

        This is called once after the import is complete so the scorer
        has fresh counts to apply the "vacuum cleaner" penalty.
        """
        self._db.execute("DELETE FROM file_statistics")
        self._db.execute(
            """
            INSERT INTO file_statistics(file_id, symbol_count)
            SELECT   file_id, COUNT(DISTINCT symbol_id)
            FROM     symbol_source_files
            GROUP BY file_id
            """
        )
        self._db.commit()

    def get_symbol_count_for_file(self, file_id: int) -> int:
        """Return how many distinct symbols reference *file_id*."""
        row = self._db.execute(
            "SELECT symbol_count FROM file_statistics WHERE file_id = ?",
            (file_id,),
        ).fetchone()
        return row["symbol_count"] if row else 0

    def get_all_file_statistics(self) -> dict[int, int]:
        """Return {file_id: symbol_count} for all files."""
        rows = self._db.execute(
            "SELECT file_id, symbol_count FROM file_statistics"
        ).fetchall()
        return {r["file_id"]: r["symbol_count"] for r in rows}

    # ------------------------------------------------------------------
    # symbol_file_candidates  (heuristic results)
    # ------------------------------------------------------------------

    def upsert_candidate(
        self,
        symbol_id: int,
        file_id: int,
        score: int,
        reason: str | None = None,
    ) -> None:
        """Insert or replace a candidate row."""
        self._db.execute(
            """
            INSERT INTO symbol_file_candidates(symbol_id, file_id, score, reason)
            VALUES (?, ?, ?, ?)
            ON CONFLICT(symbol_id, file_id) DO UPDATE
                SET score  = excluded.score,
                    reason = excluded.reason
            """,
            (symbol_id, file_id, score, reason),
        )

    def clear_candidates(self) -> None:
        """Remove all rows from symbol_file_candidates (re-analysis)."""
        self._db.execute("DELETE FROM symbol_file_candidates")
        self._db.commit()

    def get_top_candidates(
        self,
        symbol_id: int,
        limit: int = 10,
    ) -> list[tuple[int, str, int, str | None]]:
        """Return top-*limit* candidates for *symbol_id* ordered by score DESC.

        Returns [(file_id, path, score, reason), ...]
        """
        rows = self._db.execute(
            """
            SELECT sf.id, sf.path, sfc.score, sfc.reason
            FROM   symbol_file_candidates sfc
            JOIN   source_files sf ON sf.id = sfc.file_id
            WHERE  sfc.symbol_id = ?
            ORDER  BY sfc.score DESC
            LIMIT  ?
            """,
            (symbol_id, limit),
        ).fetchall()
        return [(r["id"], r["path"], r["score"], r["reason"]) for r in rows]

    def iter_all_candidates(self) -> Iterator[CandidateRow]:
        """Yield every candidate row joined with symbol/file names."""
        cur = self._db.execute(
            """
            SELECT s.name  AS symbol_name,
                   sf.path AS file_path,
                   sfc.score,
                   sfc.reason
            FROM   symbol_file_candidates sfc
            JOIN   symbols      s  ON s.id  = sfc.symbol_id
            JOIN   source_files sf ON sf.id = sfc.file_id
            ORDER  BY s.name, sfc.score DESC
            """
        )
        for row in cur:
            yield CandidateRow(
                symbol_name=row["symbol_name"],
                file_path=row["file_path"],
                score=row["score"],
                reason=row["reason"],
            )

    def candidate_count(self) -> int:
        return self._db.execute(
            "SELECT COUNT(*) FROM symbol_file_candidates"
        ).fetchone()[0]
