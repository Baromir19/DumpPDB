"""Path normalization utilities for source recovery."""

from dumppdb_tools.models import SourcePath


CODE_EXTENSIONS = frozenset({
    ".cpp",
    ".hpp",
    ".cc",
    ".cxx",
    ".c",
    ".h",
    ".hxx",
    ".inl",
})


def normalize_path(path: str) -> str:
    """Normalize backslashes to forward slashes."""
    return path.replace("\\", "/")


def normalize_extension(ext: str) -> str:
    """Ensure an extension starts with a dot."""
    if not ext.startswith("."):
        return "." + ext

    return ext


def remove_extension(path: str) -> str:
    """Remove a known source-code extension from a path."""
    lower = path.lower()

    for ext in CODE_EXTENSIONS:
        if lower.endswith(ext):
            return path[: -len(ext)]

    return path


def split_stem_ext(name: str) -> tuple[str, str]:
    """Split a file name into (stem, extension) using known extensions."""
    stem = remove_extension(name)
    return stem, name[len(stem):]


def trim_to_normalized(path: str, path_prefix: str) -> str | None:
    """Trim a raw source path to the ``normalized_paths`` format.

    Mirrors the transformation applied by ``recover_file_sources``:
    the prefix is stripped (case-insensitive), a leading slash is
    removed, and a known source extension is stripped.

    Returns ``None`` if *path* does not start with *path_prefix*.
    """
    prefix = normalize_path(path_prefix).lower()
    normalized = normalize_path(path)

    if not normalized.lower().startswith(prefix):
        return None

    normalized = normalized[len(prefix):].lstrip("/")
    normalized = remove_extension(normalized)

    return normalized or None


def normalize_paths(paths: list[str], prefix: str) -> list[SourcePath]:
    """Normalize raw PDB source paths into :class:`SourcePath` objects.

    Filters paths by a prefix, normalizes separators, and strips
    known source-code extensions.
    """
    prefix = normalize_path(prefix).lower()
    result: list[SourcePath] = []

    for path in paths:
        normalized = normalize_path(path)
        lower = normalized.lower()

        if not lower.startswith(prefix):
            continue

        # Strip prefix
        normalized = normalized[len(prefix):]

        # Strip leading slash
        normalized = normalized.lstrip("/")

        result.append(
            SourcePath(
                original=path,
                normalized=remove_extension(normalized),
            )
        )

    return result