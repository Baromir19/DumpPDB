"""SQLite schema for source path storage and type-source resolution."""


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

-- Case-insensitive index for path lookup in normalized_paths.
CREATE INDEX IF NOT EXISTS idx_normalized_paths_path_lower
    ON normalized_paths (lower(path));

-- Singleton settings table (key-value).
CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- Types discovered from the PDB.
-- disabled: 1 when the user has manually disabled this type in the GUI.
CREATE TABLE IF NOT EXISTS types (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    name     TEXT NOT NULL UNIQUE,
    disabled INTEGER NOT NULL DEFAULT 0
);

-- Type-to-type relations (template_instance, meta_variant).
CREATE TABLE IF NOT EXISTS type_relations (
    type_id         INTEGER NOT NULL,
    related_type_id INTEGER NOT NULL,
    relation_type   TEXT NOT NULL,
    FOREIGN KEY (type_id) REFERENCES types (id),
    FOREIGN KEY (related_type_id) REFERENCES types (id),
    PRIMARY KEY (type_id, related_type_id, relation_type)
);

-- Type -> source-file links. source_file_id references normalized_paths.
-- is_external: 1 when the link came from the global (all-sources) search
-- because the type's own best score never reached MIN_CONFIDENCE.
CREATE TABLE IF NOT EXISTS type_source_files (
    type_id        INTEGER NOT NULL,
    source_file_id INTEGER NOT NULL,
    score         INTEGER NOT NULL DEFAULT 0,
    user_preferred INTEGER NOT NULL DEFAULT 0,
    is_external   INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (type_id) REFERENCES types (id),
    FOREIGN KEY (source_file_id) REFERENCES normalized_paths (id),
    PRIMARY KEY (type_id, source_file_id)
);
"""