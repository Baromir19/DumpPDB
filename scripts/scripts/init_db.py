#!/usr/bin/env python3
"""Initialize the source-path SQLite database.

Creates the database and its tables if they don't exist:

    normalized_paths
    ----------------
    id
    path

    original_paths
    --------------
    id
    path
    normalized_id
    extension
"""

import argparse
import sys
from pathlib import Path

# Make the project root importable when running from the scripts/ dir
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dumppdb_tools.database import SourceDatabase


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Initialize the source-path SQLite database.",
    )

    parser.add_argument(
        "--db",
        default="sources.db",
        help="SQLite database path (default: sources.db)",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    db = SourceDatabase(args.db)

    try:
        db.open()
        print(f"Database ready: {args.db}")
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    finally:
        db.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())