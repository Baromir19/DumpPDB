"""Type exclusion pattern matching (git-ignore style)."""

import fnmatch
import logging
from pathlib import Path


def load_exclude_patterns(path: str | Path) -> list[str]:
    """Load exclude patterns from a file.

    Lines starting with ``#`` are comments; blank lines are ignored.
    Patterns support ``*`` and ``?`` wildcards via fnmatch.
    """
    exclude_path = Path(path)

    if not exclude_path.exists():
        logging.info("Exclude file not found: %s (no types excluded)", exclude_path)
        return []

    patterns: list[str] = []

    with exclude_path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()

            if not line or line.startswith("#"):
                continue

            patterns.append(line)

    logging.info("Loaded %d exclude pattern(s) from %s", len(patterns), exclude_path)
    return patterns


def is_type_excluded(type_name: str, patterns: list[str]) -> bool:
    """Return ``True`` if *type_name* matches any exclude pattern.

    Patterns use fnmatch wildcards (``*``, ``?``).
    E.g. ``std::*`` matches any type starting with ``std::``.
    """
    if not patterns:
        return False

    return any(fnmatch.fnmatch(type_name, p) for p in patterns)
