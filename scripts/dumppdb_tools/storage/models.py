"""Database schema definition.

All DDL is expressed here as Python constants.  Call
:func:`create_all` once after connecting to initialise the schema.

Table overview
--------------
symbols
    Every type / symbol name enumerated from the PDB.

source_files
    Every source file path found in the PDB (normalised to lower-case,
    with the common directory prefix stripped).

symbol_source_files
    The raw DIA link: "this symbol was compiled with this file".
    This is ground truth — data from the PDB itself.

file_statistics
    Aggregate: how many *distinct* symbols reference each file.
    Pre-computed so the scorer can apply the "vacuum cleaner" penalty
    in O(1) without a subquery.

symbol_file_candidates
    The result of the heuristic analysis: scored candidates for
    "which file is the primary home of this symbol?"
    Stores the top-N candidates per symbol together with their score
    and a short reason tag.
"""

from dumppdb_tools.storage.database import Database

# ---------------------------------------------------------------------------
# CREATE TABLE statements
# ---------------------------------------------------------------------------

_SQL_SYMBOLS = """
CREATE TABLE IF NOT EXISTS symbols (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT    NOT NULL UNIQUE
);
"""

_SQL_SOURCE_FILES = """
CREATE TABLE IF NOT EXISTS source_files (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT    NOT NULL UNIQUE
);
"""

_SQL_SYMBOL_SOURCE_FILES = """
CREATE TABLE IF NOT EXISTS symbol_source_files (
    symbol_id INTEGER NOT NULL
                      REFERENCES symbols(id),
    file_id   INTEGER NOT NULL
                      REFERENCES source_files(id),
    PRIMARY KEY (symbol_id, file_id)
);
"""

_SQL_FILE_STATISTICS = """
CREATE TABLE IF NOT EXISTS file_statistics (
    file_id      INTEGER PRIMARY KEY
                         REFERENCES source_files(id),
    symbol_count INTEGER NOT NULL DEFAULT 0
);
"""

_SQL_SYMBOL_FILE_CANDIDATES = """
CREATE TABLE IF NOT EXISTS symbol_file_candidates (
    symbol_id INTEGER NOT NULL
                      REFERENCES symbols(id),
    file_id   INTEGER NOT NULL
                      REFERENCES source_files(id),
    score     INTEGER NOT NULL,
    reason    TEXT,
    PRIMARY KEY (symbol_id, file_id)
);
"""

# ---------------------------------------------------------------------------
# Indexes for common queries
# ---------------------------------------------------------------------------

_SQL_INDEXES = [
    # Fast lookup: all files for a symbol
    "CREATE INDEX IF NOT EXISTS idx_ssf_symbol ON symbol_source_files(symbol_id);",
    # Fast lookup: all symbols for a file
    "CREATE INDEX IF NOT EXISTS idx_ssf_file   ON symbol_source_files(file_id);",
    # Fast lookup: candidates ranked by score for a symbol
    "CREATE INDEX IF NOT EXISTS idx_sfc_symbol_score "
    "ON symbol_file_candidates(symbol_id, score DESC);",
]

# All tables in creation order (respects FK dependencies)
_ALL_DDL = [
    _SQL_SYMBOLS,
    _SQL_SOURCE_FILES,
    _SQL_SYMBOL_SOURCE_FILES,
    _SQL_FILE_STATISTICS,
    _SQL_SYMBOL_FILE_CANDIDATES,
    *_SQL_INDEXES,
]


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def create_all(db: Database) -> None:
    """Create all tables and indexes if they do not already exist."""
    cur = db.cursor()
    for ddl in _ALL_DDL:
        cur.execute(ddl)
    db.commit()
