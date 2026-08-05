"""Path normalisation for source files.

The PDB stores raw paths such as::

    E:\\perforce\\example\\Game\\User.cpp
    E:\\perforce\\example\\Game\\Game\\User.h
    E:\\perforce\\example\\Game\\Game\\User.hpp

After normalisation all three become the same canonical key::

    game/user

Rules
-----
1. Strip an optional directory prefix (case-insensitive).
2. Lower-case everything.
3. Normalise separators to forward slashes.
4. Strip the file extension for code files (.cpp .cc .c .h .hpp .hxx .inl).
   Files with other extensions keep their extension so they are not
   confused with each other.

The canonical name is stored in *source_files.path*.  The scorer works
on canonical names, not on raw paths.
"""

from __future__ import annotations

import re
from pathlib import PurePosixPath, PureWindowsPath


# Extensions that get the stem-only treatment
_CODE_EXTENSIONS: frozenset[str] = frozenset(
    {".cpp", ".cc", ".c", ".h", ".hpp", ".hxx", ".inl", ".cxx"}
)


class FileNormalizer:
    """Converts a raw PDB path to a canonical, prefix-free key.

    Parameters
    ----------
    prefix:
        The common directory prefix to strip from every path, e.g.
        ``"e:\\perforce\\example"``.
        Pass an empty string (or *None*) to skip prefix stripping.
    """

    def __init__(self, prefix: str | None = None) -> None:
        # Normalise the prefix once
        if prefix:
            self._prefix: str | None = (
                self._to_forward(prefix).lower().rstrip("/") + "/"
            )
        else:
            self._prefix = None

    # ------------------------------------------------------------------
    # Public interface
    # ------------------------------------------------------------------

    def normalize(self, raw_path: str) -> str:
        """Return the canonical key for *raw_path*.

        Examples
        --------
        >>> n = FileNormalizer(r"e:\\perforce\\example")
        >>> n.normalize(r"E:\\perforce\\example\\Game\\User.cpp")
        'game/user'
        >>> n.normalize(r"E:\\perforce\\example\\Game\\User.h")
        'game/user'
        >>> n.normalize("common/config.xml")
        'common/config.xml'
        """
        path = self._to_forward(raw_path).lower()

        # Strip prefix
        if self._prefix and path.startswith(self._prefix):
            path = path[len(self._prefix):]

        path = path.lstrip("/")

        # Strip code extension
        p = PurePosixPath(path)
        if p.suffix in _CODE_EXTENSIONS:
            path = str(p.with_suffix(""))

        return path

    def normalize_many(self, raw_paths: list[str]) -> list[str]:
        """Normalise a list of paths, deduplicating the result."""
        seen: set[str] = set()
        result: list[str] = []
        for raw in raw_paths:
            canon = self.normalize(raw)
            if canon not in seen:
                seen.add(canon)
                result.append(canon)
        return result

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _to_forward(path: str) -> str:
        """Convert backslashes to forward slashes."""
        return path.replace("\\", "/")
