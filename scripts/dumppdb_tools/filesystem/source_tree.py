"""Source tree creation and renaming utilities."""

from dataclasses import dataclass
from pathlib import Path

from dumppdb_tools.models import SourcePath
from dumppdb_tools.recovery.case_dict import score
from dumppdb_tools.recovery.path_normalizer import remove_extension, split_stem_ext


def _case_rename(old_path: Path, new_path: Path) -> None:
    """Safe rename, for case-only like NTFS."""
    if str(old_path) == str(new_path):
        return

    same_dir = old_path.parent == new_path.parent
    case_only = same_dir and old_path.name.lower() == new_path.name.lower()

    if not case_only:
        old_path.rename(new_path)
        return

    tmp_path = old_path.with_name(old_path.name + ".__rename_tmp__")
    suffix = 0

    while tmp_path.exists():
        suffix += 1
        tmp_path = old_path.with_name(
            f"{old_path.name}.__rename_tmp__{suffix}"
        )

    old_path.rename(tmp_path)
    tmp_path.rename(new_path)


def _find_case_insensitive(directory: Path, name: str) -> str | None:
    if not directory.exists():
        return None

    lower = name.lower()

    for entry in directory.iterdir():
        if entry.name.lower() == lower:
            return entry.name

    return None


def create_source_tree(
    paths: list[SourcePath],
    output: Path,
    extension: str,
    template: Path | None = None,
) -> None:
    """Create an empty source tree from normalized paths."""
    content = ""

    if template:
        content = template.read_text(encoding="utf-8")

    for source in paths:
        file_path = output / (source.normalized + extension)
        file_path.parent.mkdir(parents=True, exist_ok=True)
        file_path.write_text(content, encoding="utf-8")


@dataclass(slots=True)
class RenameStats:
    """Statistics from a source tree rename operation."""

    renamed: int = 0
    already_ok: int = 0
    kept_by_score: int = 0
    collisions: int = 0
    missing: int = 0
    missing_paths: list[str] | None = None

    def __post_init__(self):
        if self.missing_paths is None:
            self.missing_paths = []


def rename_source_tree(
    paths: list[SourcePath],
    output: Path,
    verbose: bool = False,
) -> RenameStats:
    """Rename an existing source tree in place using score comparison.

    Returns a :class:`RenameStats` with the operation summary.
    """
    stats = RenameStats()

    print(f"Searching in: {output}")
    print(f"Recovered paths: {len(paths)}")
    print()

    for source in paths:
        _rename_one(source, output, stats, verbose)

    print()
    print("Rename summary")
    print(f"  Renamed      : {stats.renamed}")
    print(f"  Already OK   : {stats.already_ok}")
    print(f"  Kept by score: {stats.kept_by_score}")
    print(f"  Collisions   : {stats.collisions}")
    print(f"  Missing      : {stats.missing}")

    if verbose and stats.missing_paths:
        print("\nMissing paths:")
        for path in stats.missing_paths:
            print(f"  {path}")

    return stats


def _rename_one(
    source: SourcePath,
    output: Path,
    stats: RenameStats,
    verbose: bool,
) -> None:
    target_parts = source.normalized.split("/")
    dir_parts = target_parts[:-1]
    stem_target = target_parts[-1]

    current_path = output
    found = True

    for part in dir_parts:
        existing_name = _find_case_insensitive(current_path, part)

        if existing_name is None:
            found = False
            break

        if existing_name != part:
            old_score = score(existing_name)
            new_score = score(part)

            if new_score > old_score:
                new_dir_path = current_path / part
                old_dir_path = current_path / existing_name

                if new_dir_path.exists() and new_dir_path != old_dir_path:
                    stats.collisions += 1
                    if verbose:
                        print(f"Skip dir (target exists): {old_dir_path} -> {new_dir_path}")
                else:
                    _case_rename(old_dir_path, new_dir_path)
                    existing_name = part
                    stats.renamed += 1
                    if verbose:
                        print(f"{old_dir_path} -> {new_dir_path}")
            else:
                stats.kept_by_score += 1
        else:
            stats.already_ok += 1

        current_path = current_path / existing_name

    if not found or not current_path.exists():
        stats.missing += 1
        stats.missing_paths.append(source.normalized)
        return

    matches = [
        entry
        for entry in current_path.iterdir()
        if entry.is_file()
        and remove_extension(entry.name).lower() == stem_target.lower()
    ]

    if not matches:
        stats.missing += 1
        stats.missing_paths.append(source.normalized)
        return

    for entry in matches:
        entry_stem, entry_ext = split_stem_ext(entry.name)

        if entry_stem == stem_target:
            stats.already_ok += 1
            continue

        old_score = score(entry_stem)
        new_score = score(stem_target)

        if new_score <= old_score:
            stats.kept_by_score += 1
            continue

        new_path = entry.with_name(stem_target + entry_ext)

        if new_path.exists() and new_path != entry:
            stats.collisions += 1
            if verbose:
                print(f"Skip (target exists): {entry} -> {new_path}")
            continue

        _case_rename(entry, new_path)
        stats.renamed += 1

        if verbose:
            print(f"{entry} -> {new_path}")