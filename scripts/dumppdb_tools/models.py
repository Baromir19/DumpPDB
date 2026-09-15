"""Shared data models."""

from dataclasses import dataclass


@dataclass(slots=True)
class SourcePath:
    """A source file path with original and normalized forms."""

    original: str
    normalized: str


@dataclass(slots=True)
class SourceRecord:
    """A database record for a source path.

    Attributes:
        id: Database row id.
        path: Original path as stored in the PDB.
        normalized_id: Foreign key to :class:`NormalizedPath`.
        extension: File extension (normalized, e.g. ``.cpp``).
    """

    id: int
    path: str
    normalized_id: int
    extension: str


@dataclass(slots=True)
class NormalizedPath:
    """A normalized path record in the database."""

    id: int
    path: str