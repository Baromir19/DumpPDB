#!/usr/bin/env python3

import argparse
import sqlite3
import sys

from dumppdb_tools import PdbClient


def main():
    parser = argparse.ArgumentParser(
        description="Disable types that cannot be found in the PDB."
    )

    parser.add_argument(
        "--db",
        required=True,
        help="Path to SQLite database",
    )

    parser.add_argument(
        "--pdb",
        required=True,
        help="Path to the PDB file",
    )

    parser.add_argument(
        "--dll",
        default="./PdbAPI.dll",
        help="Path to PdbAPI.dll",
    )

    args = parser.parse_args()

    conn = sqlite3.connect(args.db)
    conn.row_factory = sqlite3.Row

    pdb = PdbClient(args.dll)

    try:
        pdb.open(args.pdb)

        types = conn.execute(
            """
            SELECT id, name, disabled
            FROM types
            ORDER BY id
            """
        ).fetchall()

        total = len(types)

        skipped_disabled = 0
        skipped_relations = 0
        checked = 0
        found = 0
        disabled = 0

        print(f"Total types: {total}")
        print()

        for row in types:
            type_id = row["id"]
            type_name = row["name"]

            if row["disabled"]:
                skipped_disabled += 1
                continue

            relation_exists = conn.execute(
                """
                SELECT 1
                FROM type_relations
                WHERE type_id = ?
                   OR related_type_id = ?
                LIMIT 1
                """,
                (type_id, type_id),
            ).fetchone()

            if relation_exists:
                skipped_relations += 1
                continue

            checked += 1

            try:
                pdb.dump_type(type_name, True)

                found += 1

                print(
                    f"[FOUND]   {type_id}: {type_name}"
                )

            except Exception as e:
                disabled += 1

                print(
                    f"[DISABLE] {type_id}: {type_name}"
                )
                print(
                    f"          {e}"
                )

                conn.execute(
                    """
                    UPDATE types
                    SET disabled = 1
                    WHERE id = ?
                    """,
                    (type_id,),
                )

                conn.commit()

        print()
        print("Done.")
        print(f"Total:             {total}")
        print(f"Skipped disabled:  {skipped_disabled}")
        print(f"Skipped relations: {skipped_relations}")
        print(f"Checked:           {checked}")
        print(f"Found:             {found}")
        print(f"Disabled:          {disabled}")

    except Exception as e:
        print(f"Fatal error: {e}", file=sys.stderr)
        return 1

    finally:
        pdb.close()
        conn.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())