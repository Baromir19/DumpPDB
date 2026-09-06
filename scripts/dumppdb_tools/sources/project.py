"""Probing a reconstructed source tree for existing source files.

These helpers answer one question that several CLI tools share: given a tree
on disk (the reconstructed project), which of the type sources recorded in the
DumpPDB database are already present? ``reconstruct_sources`` uses this to
skip types that need no work, and ``compare_type_sources`` uses the shared
path-normalisation when cross-checking recorded paths.
"""

from pathlib import Path

from dumppdb_tools.sources.type_sources import TypeSource

# File extensions considered when scanning a (reconstructed) source tree.
SOURCE_EXTENSIONS = frozenset(
    {".h", ".hpp", ".hxx", ".hh", ".inl", ".c", ".cc", ".cxx", ".cpp"}
)


def walk_project_sources(source_path: str | Path):
    """Yield every supported source file under *source_path*, sorted.

    Files are yielded in deterministic (path-sorted) order. A missing root
    yields nothing.
    """
    root = Path(source_path)

    if not root.exists():
        return

    for file_path in sorted(root.rglob("*")):
        if not file_path.is_file():
            continue
        if file_path.suffix.lower() not in SOURCE_EXTENSIONS:
            continue
        yield file_path


def _normalized_relative(file_path: Path, root: Path) -> str:
    """Lowercased, extension-stripped, forward-slash path relative to *root*."""
    relative = file_path.relative_to(root).as_posix()
    return Path(relative).with_suffix("").as_posix().lower()


def collect_project_paths(source_path: str | Path) -> set[str]:
    """Collect the normalized relative paths already present in the tree.

    Returns a set of lowercased, extension-stripped, forward-slash paths, so
    lookups are case-insensitive — mirroring how ``normalized_paths`` are
    stored in the database.
    """
    root = Path(source_path)

    if not root.exists():
        return set()

    return {_normalized_relative(p, root) for p in walk_project_sources(root)}


def paths_match(expected: str, actual: str) -> bool:
    """Case-insensitive containment/equality of two paths.

    Separators are normalised to ``/`` and both sides are lowercased. An
    expected path is considered present in *actual* as soon as it appears as a
    substring of it, so a stored relative path matches a fully-qualified file
    path that still carries its extension.
    """

    def norm(p: str | None) -> str:
        return (p or "").replace("\\", "/").strip().lower()

    a, b = norm(expected), norm(actual)

    if not a or not b:
        return True

    return a == b or a in b or b in a


def _is_present(item: TypeSource, present_paths: set[str]) -> bool:
    if item.path is None:
        return False

    return item.path.lower() in present_paths


def missing_from_project(
    items: list[TypeSource],
    source_path: str | Path,
    *,
    present_paths: set[str] | None = None,
) -> list[TypeSource]:
    """Return the *items* whose source is not yet present in the project tree.

    A type is considered already reconstructed when a file whose normalized
    relative path matches its recorded ``path`` exists under *source_path*.
    ``present_paths`` (from :func:`collect_project_paths`) may be supplied to
    avoid re-scanning the tree across repeated calls.
    """
    if present_paths is None:
        present_paths = collect_project_paths(source_path)

    return [item for item in items if not _is_present(item, present_paths)]