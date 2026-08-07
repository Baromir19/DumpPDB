"""SQLite schema for source path storage."""


SCHEMA = """
CREATE TABLE IF NOT EXISTS normalized_paths (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS original_paths (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    path          TEXT NOT NULL,
    normalized_id INTEGER NOT NULL,
    extension     TEXT NOT NULL,
    FOREIGN KEY (normalized_id) REFERENCES normalized_paths (id)
);

CREATE INDEX IF NOT EXISTS idx_original_paths_normalized_id
    ON original_paths (normalized_id);
"""