#!/usr/bin/env python3

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys

from dumppdb_tools import PdbClient


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


@dataclass(slots=True)
class SourcePath:
    original: str
    normalized: str

def _case_rename(old_path: Path, new_path: Path) -> None:
    """
    Safe rename, for chey-only like NTFS
    """
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

def score(value: str) -> int:
    upper = sum(c.isupper() for c in value)
    lower = sum(c.islower() for c in value)

    if upper == 0:
        return 0

    if lower == 0:
        return 1

    return upper * 10 + len(value)

def add_case_variant(storage: dict[str, tuple[str, int]], value: str):
    key = value.lower()

    new_score = score(value)

    old = storage.get(key)

    if old is None or new_score > old[1]:
        storage[key] = (value, new_score)


def remove_extension(path: str) -> str:
    lower = path.lower()

    for ext in CODE_EXTENSIONS:
        if lower.endswith(ext):
            return path[:-len(ext)]

    return path

def normalize_extension(ext: str) -> str:
    if not ext.startswith("."):
        return "." + ext

    return ext

def create_source_tree(
    paths: list[SourcePath],
    output: Path,
    extension: str,
    template: Path | None,
):
    content = ""

    if template:
        content = template.read_text(
            encoding="utf-8"
        )

    for source in paths:
        file_path = output / (
            source.normalized + extension
        )

        file_path.parent.mkdir(
            parents=True,
            exist_ok=True,
        )

        file_path.write_text(
            content,
            encoding="utf-8",
        )

def _split_stem_ext(name: str) -> tuple[str, str]:
    stem = remove_extension(name)
    return stem, name[len(stem):]


def rename_source_tree(
    paths: list[SourcePath],
    output: Path,
    verbose: bool = False,
) -> None:
    renamed = 0
    already_ok = 0
    kept_by_score = 0
    collisions = 0
    missing = 0

    missing_paths: list[str] = []

    print(f"Searching in: {output}")
    print(f"Recovered paths: {len(paths)}")
    print()

    for source in paths:
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
                        collisions += 1
                        if verbose:
                            print(f"Skip dir (target exists): {old_dir_path} -> {new_dir_path}")
                    else:
                        _case_rename(old_dir_path, new_dir_path)
                        existing_name = part
                        renamed += 1
                        if verbose:
                            print(f"{old_dir_path} -> {new_dir_path}")
                else:
                    kept_by_score += 1
            else:
                already_ok += 1

            current_path = current_path / existing_name

        if not found or not current_path.exists():
            missing += 1
            missing_paths.append(source.normalized)
            continue

        matches = [
            entry
            for entry in current_path.iterdir()
            if entry.is_file()
            and remove_extension(entry.name).lower() == stem_target.lower()
        ]

        if not matches:
            missing += 1
            missing_paths.append(source.normalized)
            continue

        for entry in matches:
            entry_stem, entry_ext = _split_stem_ext(entry.name)

            if entry_stem == stem_target:
                already_ok += 1
                continue

            old_score = score(entry_stem)
            new_score = score(stem_target)

            if new_score <= old_score:
                kept_by_score += 1
                continue

            new_path = entry.with_name(stem_target + entry_ext)

            if new_path.exists() and new_path != entry:
                collisions += 1
                if verbose:
                    print(f"Skip (target exists): {entry} -> {new_path}")
                continue

            _case_rename(entry, new_path)
            renamed += 1

            if verbose:
                print(f"{entry} -> {new_path}")

    print()
    print("Rename summary")
    print(f"  Renamed      : {renamed}")
    print(f"  Already OK   : {already_ok}")
    print(f"  Kept by score: {kept_by_score}")
    print(f"  Collisions   : {collisions}")
    print(f"  Missing      : {missing}")

    if verbose and missing_paths:
        print("\nMissing paths:")
        for path in missing_paths:
            print(f"  {path}")


def _split_ext(segment: str, extension: str, is_file: bool) -> tuple[str, str]:
    if is_file and segment.lower().endswith(extension.lower()):
        return segment[: -len(extension)], segment[-len(extension):]
    return segment, ""


def _find_case_insensitive(directory: Path, name: str) -> str | None:
    if not directory.exists():
        return None

    lower = name.lower()

    for entry in directory.iterdir():
        if entry.name.lower() == lower:
            return entry.name

    return None

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Reconstruct source tree from PDB and executable."
    )

    parser.add_argument(
        "--dll",
        default="./PdbAPI.dll",
        help="Path to PdbAPI.dll",
    )

    parser.add_argument(
        "--pdb",
        required=True,
        help="Path to PDB file",
    )

    parser.add_argument(
        "--exe",
        required=True,
        help="Path to executable or DLL used for path recovery",
    )

    parser.add_argument(
        "--path-prefix",
        required=True,
        help="Only process source files whose path begins with this prefix",
    )

    parser.add_argument(
        "--output",
        default="reconstructed",
        help="Output directory for reconstructed source tree",
    )

    parser.add_argument(
        "--output-extension",
        default=".hpp",
        help="Extension for generated files (default: .hpp)",
    )

    parser.add_argument(
        "--template",
        help="Header template copied into every generated file",
    )

    parser.add_argument(
        "--db",
        default="sources.db",
        help="SQLite database path",
    )

    parser.add_argument(
        "--write-db",
        action="store_true",
        help="Write recovered paths into the database",
    )

    parser.add_argument(
        "--create-tree",
        action="store_true",
        help="Create reconstructed directory tree",
    )

    parser.add_argument(
        "--rename-tree",
        action="store_true",
        help="Rename existing reconstructed tree in place using score comparison, without creating new files",
    )

    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print additional information",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.create_tree and args.template:
        template_path = Path(args.template)

        if not template_path.exists():
            print(
                f"Error: template file not found: {template_path}",
                file=sys.stderr,
            )
            return 1

    output_extension = normalize_extension(
        args.output_extension
    )

    pdb = PdbClient(args.dll)

    try:
        pdb.open(args.pdb)

        print("DLL           :", args.dll)
        print("PDB           :", args.pdb)
        print("EXE           :", args.exe)
        print("Path prefix   :", args.path_prefix)
        print("Output        :", args.output)
        print("Template      :", args.template or "<empty>")
        print("Database      :", args.db)
        print("Create tree   :", args.create_tree)
        print("Write DB      :", args.write_db)
        print()

        print("Enumerating source files...\n")

        raw = pdb.enumerate_source_files()
        paths = [p.strip() for p in raw.splitlines() if p.strip()]

        prefix = args.path_prefix.replace("\\", "/").lower()

        def normalize_source_path(path: str, prefix: str) -> str | None:
            normalized = path.replace("\\", "/")

            lower = normalized.lower()

            if not lower.startswith(prefix):
                return None

            # prefix
            normalized = normalized[len(prefix):]

            # /
            normalized = normalized.lstrip("/")

            return remove_extension(normalized)

        filtered: list[SourcePath] = []

        for path in paths:
            normalized = normalize_source_path(path, prefix)

            if normalized:
                filtered.append(
                    SourcePath(
                        original=path,
                        normalized=normalized,
                    )
                )

        def get_exe_string_dict():
            # /[^:*?"<>|\r\n]+\.(cpp|hpp|c|h)
            # [A-Za-z]:[\\/][^:*?"<>|\r\n]+\.(cpp|hpp|c|h|cc|cxx|hxx)
            # .*\.(cpp|hpp|c|h|cc|cxx|hxx)$

            exe_raw_strings = pdb.find_strings(
                args.exe,
                min_length=5,
                encodings=1 | 2 | 4,
                section_names="",
                regex_pattern="",
            )

            parts = {}

            # files = {}

            for line in exe_raw_strings.splitlines():

                path = line.strip()

                if not path.lower().endswith(tuple(CODE_EXTENSIONS)):
                    continue

                path = path.replace("\\", "/")

                # files[path.lower()] = path

                for part in path.split("/"):

                    if not part:
                        continue

                    part = remove_extension(part)

                    add_case_variant(parts, part)


            return {
                key: value[0]
                for key, value in parts.items()
            }

        def get_pdb_types():
            symbols = pdb.enumerate_symbols()

            return {
                symbol.strip().lower(): symbol.strip()
                for symbol in symbols.splitlines()
                if symbol.strip()
            }

        case_dict = {}

        path_parts = get_exe_string_dict()

        for key, value in path_parts.items():
            add_case_variant(case_dict, value)

        types = get_pdb_types()

        for value in types.values():
            add_case_variant(case_dict, value)

        for path in filtered:
            parts = path.normalized.split("/")

            for i, part in enumerate(parts):
                candidate = case_dict.get(part.lower())

                if candidate:
                    parts[i] = candidate[0]

            path.normalized = "/".join(parts)

        if not args.create_tree and not args.write_db and not args.rename_tree:
            for path in filtered:
                print(path.normalized)

            return 0

        if args.create_tree:
            create_source_tree(
                filtered,
                Path(args.output),
                output_extension,
                Path(args.template)
                if args.template
                else None,
            )

        if args.rename_tree:
            rename_source_tree(
                filtered,
                Path(args.output),
                verbose=args.verbose,
            )

        # TODO:
        # 5. Write database

    except Exception as e:
        print(f"Error: {pdb.last_error()}")
        print(f"Exception: {e}")
        return 1

    finally:
        pdb.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())