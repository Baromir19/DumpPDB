"""Heuristic scorer: how well does a file match a symbol name?

Scoring rules
-------------
+200  Exact basename match after stripping common suffixes
      (e.g. "ActorDefinition" → stem "actor" == file stem "actor")
+100  Full symbol name (sans suffix) equals the file stem exactly
 +20  A CamelCase word from the symbol appears in the file stem
  +5  A CamelCase word from the symbol appears anywhere in the path
 -50  File has > 500 distinct symbols  ("popular" file penalty)
-150  File has > 2000 distinct symbols ("vacuum cleaner" penalty)

The reason tag is the *primary* scoring rule that triggered the highest
bonus.  It is stored in *symbol_file_candidates.reason* for debugging.
"""

from __future__ import annotations

import re
from pathlib import PurePosixPath


# Common type suffixes that are stripped before name comparison
_STRIP_SUFFIXES: tuple[str, ...] = (
    "definition",
    "descriptor",
    "manager",
    "handler",
    "controller",
    "factory",
    "helper",
    "util",
    "utils",
    "interface",
    "impl",
    "base",
)

# Thresholds for the "vacuum cleaner" penalty
_PENALTY_THRESHOLD_LOW  = 500
_PENALTY_THRESHOLD_HIGH = 2000
_PENALTY_LOW            = -50
_PENALTY_HIGH           = -150


def _split_camel(name: str) -> list[str]:
    """Split a CamelCase identifier into lower-case words of length > 2.

    >>> _split_camel("VehiclePhysicsDefinition")
    ['vehicle', 'physics', 'definition']
    """
    words = re.findall(r"[A-Z][a-z0-9]*", name)
    return [w.lower() for w in words if len(w) > 2]


def _strip_suffix(name_lower: str) -> str:
    """Remove a known trailing suffix from a lower-cased name."""
    for suffix in _STRIP_SUFFIXES:
        if name_lower.endswith(suffix) and len(name_lower) > len(suffix):
            return name_lower[: -len(suffix)]
    return name_lower


class TypeFileScorer:
    """Scores a (symbol_name, file_path) pair.

    Parameters
    ----------
    file_stats:
        A mapping ``{file_id: symbol_count}`` used to apply the
        popularity penalty.  Pass an empty dict to skip the penalty.
    """

    def __init__(self, file_stats: dict[int, int] | None = None) -> None:
        self._file_stats: dict[int, int] = file_stats or {}

    # ------------------------------------------------------------------
    # Public interface
    # ------------------------------------------------------------------

    def score(
        self,
        symbol_name: str,
        file_path: str,        # canonical (normalised, no extension)
        file_id: int | None = None,
    ) -> tuple[int, str]:
        """Return *(score, reason)* for the given pair.

        Parameters
        ----------
        symbol_name:
            Raw symbol name as stored in *symbols.name*.
        file_path:
            Canonical file path (lower-case, extension stripped) as
            stored in *source_files.path*.
        file_id:
            Optional id used to look up the popularity penalty.
        """
        name_lower = symbol_name.lower()
        base_name  = _strip_suffix(name_lower)
        words      = _split_camel(symbol_name)

        # The last component of the file path (stem)
        file_stem = PurePosixPath(file_path).name

        score  = 0
        reason = "path_match"  # default — weakest

        # --- Bonus: exact stem match after suffix stripping ---
        if file_stem == base_name:
            score  += 200
            reason  = "name_match_stripped"
        elif file_stem == name_lower:
            score  += 100
            reason  = "name_match_exact"
        else:
            # --- Bonus: word matches ---
            stem_bonus = 0
            path_bonus = 0
            for word in words:
                if word in file_stem:
                    stem_bonus += 20
                elif word in file_path:
                    path_bonus += 5

            if stem_bonus > 0:
                score  += stem_bonus
                reason  = "word_match_stem"
            elif path_bonus > 0:
                score  += path_bonus
                reason  = "word_match_path"

        # --- Penalty: vacuum-cleaner files ---
        if file_id is not None and file_id in self._file_stats:
            count = self._file_stats[file_id]
            if count > _PENALTY_THRESHOLD_HIGH:
                score += _PENALTY_HIGH
            elif count > _PENALTY_THRESHOLD_LOW:
                score += _PENALTY_LOW

        return score, reason

    def update_stats(self, file_stats: dict[int, int]) -> None:
        """Replace the cached file statistics."""
        self._file_stats = file_stats
