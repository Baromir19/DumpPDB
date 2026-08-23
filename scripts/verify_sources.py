#!/usr/bin/env python3
"""GUI for reviewing and manually resolving type -> source-file mappings."""

from __future__ import annotations

import argparse
import sqlite3
import sys
import tkinter as tk
from dataclasses import dataclass
from pathlib import Path
from tkinter import messagebox, ttk

from dumppdb_tools.resolution.scorer import LOW_CONFIDENCE_GAP, MIN_CONFIDENCE


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------

@dataclass
class SourceCandidate:
    source_file_id: int
    path: str
    score: int
    user_preferred: bool


@dataclass
class TypeResolution:
    type_id: int
    name: str
    sources: list[SourceCandidate]

    @property
    def status(self) -> str:
        if not self.sources:
            return "no_match"

        best_score = max(source.score for source in self.sources)

        if best_score == 0:
            return "no_match"

        tied = [
            source
            for source in self.sources
            if source.score == best_score
        ]

        if len(tied) > 1:
            return "ambiguous"

        if best_score < MIN_CONFIDENCE:
            return "below_confidence"

        if len(self.sources) > 1:
            scores = sorted(
                (source.score for source in self.sources),
                reverse=True,
            )

            second_score = scores[1]

            if (
                best_score - second_score
                < best_score * LOW_CONFIDENCE_GAP
            ):
                return "low_confidence"

        return "resolved"

    @property
    def user_preferred(self) -> bool:
        return any(
            source.user_preferred
            for source in self.sources
        )


# ---------------------------------------------------------------------------
# Database
# ---------------------------------------------------------------------------

class ResolutionDatabase:
    """Read/write access to the resolution database."""

    def __init__(self, path: str | Path):
        self.path = str(path)
        self.conn: sqlite3.Connection | None = None

    def open(self) -> None:
        self.conn = sqlite3.connect(self.path)

    def close(self) -> None:
        if self.conn is not None:
            self.conn.close()
            self.conn = None

    def __enter__(self) -> "ResolutionDatabase":
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def load_resolutions(self) -> list[TypeResolution]:
        assert self.conn is not None

        # Types that appear as related_type_id are handled via their
        # relation source (parent type); exclude them from review.
        related_ids = {
            row[0]
            for row in self.conn.execute(
                """
                SELECT related_type_id
                FROM type_relations
                """
            ).fetchall()
        }

        rows = self.conn.execute(
            """
            SELECT
                t.id,
                t.name,
                np.id,
                np.path,
                tsf.score,
                tsf.user_preferred
            FROM types AS t
            LEFT JOIN type_source_files AS tsf
                ON tsf.type_id = t.id
            LEFT JOIN normalized_paths AS np
                ON np.id = tsf.source_file_id
            ORDER BY
                t.name COLLATE NOCASE,
                tsf.score DESC,
                np.path COLLATE NOCASE
            """
        ).fetchall()

        rows = [
            row
            for row in rows
            if row[0] not in related_ids
        ]

        result: dict[int, TypeResolution] = {}

        for (
            type_id,
            type_name,
            source_file_id,
            path,
            score,
            user_preferred,
        ) in rows:
            if type_id not in result:
                result[type_id] = TypeResolution(
                    type_id=type_id,
                    name=type_name,
                    sources=[],
                )

            if source_file_id is not None:
                result[type_id].sources.append(
                    SourceCandidate(
                        source_file_id=source_file_id,
                        path=path,
                        score=score,
                        user_preferred=bool(user_preferred),
                    )
                )

        return list(result.values())

    def search_paths(self, query: str) -> list[tuple[int, str]]:
        assert self.conn is not None

        if not query:
            rows = self.conn.execute(
                """
                SELECT id, path
                FROM normalized_paths
                ORDER BY path COLLATE NOCASE
                LIMIT 500
                """
            ).fetchall()
        else:
            rows = self.conn.execute(
                """
                SELECT id, path
                FROM normalized_paths
                WHERE path LIKE ? COLLATE NOCASE
                ORDER BY path COLLATE NOCASE
                LIMIT 500
                """,
                (f"%{query}%",),
            ).fetchall()

        return rows

    def set_user_preferred(
        self,
        type_id: int,
        source_file_id: int,
        preferred: bool,
    ) -> None:
        assert self.conn is not None

        self.conn.execute(
            """
            UPDATE type_source_files
            SET user_preferred = ?
            WHERE type_id = ?
              AND source_file_id = ?
            """,
            (
                int(preferred),
                type_id,
                source_file_id,
            ),
        )

    def add_manual_relation(
        self,
        type_id: int,
        source_file_id: int,
    ) -> None:
        assert self.conn is not None

        # Existing relation:
        # preserve its score, only make it preferred.
        cursor = self.conn.execute(
            """
            UPDATE type_source_files
            SET user_preferred = 1
            WHERE type_id = ?
              AND source_file_id = ?
            """,
            (
                type_id,
                source_file_id,
            ),
        )

        if cursor.rowcount == 0:
            self.conn.execute(
                """
                INSERT INTO type_source_files (
                    type_id,
                    source_file_id,
                    score,
                    user_preferred
                )
                VALUES (?, ?, 0, 1)
                """,
                (
                    type_id,
                    source_file_id,
                ),
            )

        self.conn.commit()

    def save_preferred_changes(
        self,
        changes: dict[tuple[int, int], bool],
    ) -> None:
        assert self.conn is not None

        try:
            self.conn.execute("BEGIN")

            for (
                type_id,
                source_file_id,
            ), preferred in changes.items():
                self.conn.execute(
                    """
                    UPDATE type_source_files
                    SET user_preferred = ?
                    WHERE type_id = ?
                      AND source_file_id = ?
                    """,
                    (
                        int(preferred),
                        type_id,
                        source_file_id,
                    ),
                )

            self.conn.commit()

        except Exception:
            self.conn.rollback()
            raise


# ---------------------------------------------------------------------------
# UI
# ---------------------------------------------------------------------------

STATUS_TEXT = {
    "resolved": "Resolved",
    "ambiguous": "Ambiguous",
    "low_confidence": "Low confidence",
    "below_confidence": "Below confidence",
    "no_match": "No match",
}

STATUS_COLORS = {
    "resolved": "#2e8b57",
    "ambiguous": "#d32f2f",
    "low_confidence": "#f0a000",
    "below_confidence": "#e65100",
    "no_match": "#808080",
}

STATUS_BG = {
    "resolved": "#e8f5e9",
    "ambiguous": "#ffebee",
    "low_confidence": "#fff8e1",
    "below_confidence": "#fff3e0",
    "no_match": "#f5f5f5",
}

PREFERRED_COLOR = "#1976d2"


class ResolutionUI(tk.Tk):
    """Main resolution UI."""

    def __init__(self, db_path: str):
        super().__init__()

        self.title("PDB Type Source Resolution")
        self.geometry("1100x750")
        self.minsize(800, 500)

        self.db = ResolutionDatabase(db_path)

        try:
            self.db.open()
        except sqlite3.Error as exc:
            messagebox.showerror(
                "Database error",
                f"Could not open database:\n\n{exc}",
            )
            self.destroy()
            return

        self.resolutions: list[TypeResolution] = []

        # Pending changes:
        # (type_id, source_file_id) -> preferred
        self.pending_changes: dict[
            tuple[int, int],
            bool,
        ] = {}

        # Tree item -> TypeResolution
        self.type_items: dict[str, TypeResolution] = {}

        # Tree item -> SourceCandidate
        self.source_items: dict[
            str,
            tuple[TypeResolution, SourceCandidate],
        ] = {}

        self.filter_var = tk.StringVar(value="all")
        self.search_var = tk.StringVar()

        self._create_widgets()
        self._configure_tags()
        self.reload()

        self.protocol(
            "WM_DELETE_WINDOW",
            self._on_close,
        )

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _create_widgets(self) -> None:
        toolbar = ttk.Frame(self, padding=8)
        toolbar.pack(fill=tk.X)

        ttk.Label(
            toolbar,
            text="Filter:",
        ).pack(side=tk.LEFT)

        filter_box = ttk.Combobox(
            toolbar,
            textvariable=self.filter_var,
            state="readonly",
            width=18,
            values=[
                "all",
                "resolved",
                "ambiguous",
                "low_confidence",
                "below_confidence",
                "user_preferred",
                "no_match",
            ],
        )
        filter_box.pack(
            side=tk.LEFT,
            padx=(6, 20),
        )
        filter_box.bind(
            "<<ComboboxSelected>>",
            lambda _: self.refresh_tree(),
        )

        ttk.Label(
            toolbar,
            text="Search:",
        ).pack(side=tk.LEFT)

        search = ttk.Entry(
            toolbar,
            textvariable=self.search_var,
        )
        search.pack(
            side=tk.LEFT,
            fill=tk.X,
            expand=True,
        )
        search.bind(
            "<KeyRelease>",
            lambda _: self.refresh_tree(),
        )

        ttk.Button(
            toolbar,
            text="Refresh",
            command=self.reload,
        ).pack(
            side=tk.LEFT,
            padx=(8, 0),
        )

        # --------------------------------------------------------------
        # Tree
        # --------------------------------------------------------------

        tree_frame = ttk.Frame(self)
        tree_frame.pack(
            fill=tk.BOTH,
            expand=True,
            padx=8,
        )

        self.tree = ttk.Treeview(
            tree_frame,
            columns=("score", "status"),
            show="tree headings",
            selectmode="browse",
        )

        self.tree.heading(
            "#0",
            text="Type / Source file",
        )

        self.tree.heading(
            "score",
            text="Score",
        )

        self.tree.heading(
            "status",
            text="Status",
        )

        self.tree.column(
            "#0",
            width=700,
            anchor=tk.W,
        )

        self.tree.column(
            "score",
            width=100,
            anchor=tk.E,
        )

        self.tree.column(
            "status",
            width=160,
            anchor=tk.CENTER,
        )

        scrollbar = ttk.Scrollbar(
            tree_frame,
            orient=tk.VERTICAL,
            command=self.tree.yview,
        )

        self.tree.configure(
            yscrollcommand=scrollbar.set,
        )

        self.tree.pack(
            side=tk.LEFT,
            fill=tk.BOTH,
            expand=True,
        )

        scrollbar.pack(
            side=tk.RIGHT,
            fill=tk.Y,
        )

        self.tree.bind(
            "<Double-1>",
            self._on_double_click,
        )

        self.tree.bind(
            "<Return>",
            self._on_return,
        )

        # --------------------------------------------------------------
        # Bottom controls
        # --------------------------------------------------------------

        bottom = ttk.Frame(
            self,
            padding=8,
        )
        bottom.pack(fill=tk.X)

        ttk.Button(
            bottom,
            text="Add manual relation",
            command=self.add_manual_relation,
        ).pack(side=tk.LEFT)

        ttk.Label(
            bottom,
            text=(
                "Double-click a source file to mark it as "
                "user preferred."
            ),
        ).pack(
            side=tk.LEFT,
            padx=15,
        )

        self.pending_label = ttk.Label(
            bottom,
            text="No unsaved changes",
        )
        self.pending_label.pack(
            side=tk.LEFT,
            padx=10,
        )

        ttk.Button(
            bottom,
            text="Save",
            command=self.save,
        ).pack(
            side=tk.RIGHT,
        )

    def _configure_tags(self) -> None:
        for status, color in STATUS_COLORS.items():
            self.tree.tag_configure(
                f"type_{status}",
                foreground=color,
                background=STATUS_BG[status],
            )

        self.tree.tag_configure(
            "preferred",
            foreground=PREFERRED_COLOR,
        )

        self.tree.tag_configure(
            "source",
            foreground="#333333",
        )

        self.tree.tag_configure(
            "source_preferred",
            foreground=PREFERRED_COLOR,
        )

    # ------------------------------------------------------------------
    # Loading
    # ------------------------------------------------------------------

    def reload(self) -> None:
        try:
            self.resolutions = self.db.load_resolutions()
        except sqlite3.Error as exc:
            messagebox.showerror(
                "Database error",
                f"Could not read database:\n\n{exc}",
            )
            return

        self.pending_changes.clear()
        self.refresh_tree()

    # ------------------------------------------------------------------
    # Filtering
    # ------------------------------------------------------------------

    def _matches_filter(
        self,
        resolution: TypeResolution,
    ) -> bool:
        selected = self.filter_var.get()

        if selected == "all":
            return True

        if selected == "user_preferred":
            return resolution.user_preferred

        return resolution.status == selected

    def _matches_search(
        self,
        resolution: TypeResolution,
    ) -> bool:
        query = self.search_var.get().strip().lower()

        if not query:
            return True

        if query in resolution.name.lower():
            return True

        return any(
            query in source.path.lower()
            for source in resolution.sources
        )

    # ------------------------------------------------------------------
    # Tree rendering
    # ------------------------------------------------------------------

    def refresh_tree(self) -> None:
        for item in self.tree.get_children():
            self.tree.delete(item)

        self.type_items.clear()
        self.source_items.clear()

        for resolution in self.resolutions:

            if not self._matches_filter(resolution):
                continue

            if not self._matches_search(resolution):
                continue

            status = resolution.status

            if resolution.user_preferred:
                type_tag = "preferred"
            else:
                type_tag = f"type_{status}"

            label = resolution.name

            if resolution.user_preferred:
                label += "  ★"

            type_item = self.tree.insert(
                "",
                tk.END,
                text=label,
                values=(
                    "",
                    STATUS_TEXT.get(
                        status,
                        status,
                    ),
                ),
                tags=(type_tag,),
                open=False,
            )

            self.type_items[type_item] = resolution

            for source in sorted(
                resolution.sources,
                key=lambda item: (
                    -item.score,
                    item.path.lower(),
                ),
            ):
                preferred = self._effective_preferred(
                    resolution.type_id,
                    source,
                )

                source_label = source.path

                if preferred:
                    source_label += "  ★"

                tag = (
                    "source_preferred"
                    if preferred
                    else "source"
                )

                source_item = self.tree.insert(
                    type_item,
                    tk.END,
                    text=source_label,
                    values=(
                        source.score,
                        "",
                    ),
                    tags=(tag,),
                )

                self.source_items[source_item] = (
                    resolution,
                    source,
                )

        self._update_pending_label()

    def _effective_preferred(
        self,
        type_id: int,
        source: SourceCandidate,
    ) -> bool:
        key = (
            type_id,
            source.source_file_id,
        )

        if key in self.pending_changes:
            return self.pending_changes[key]

        return source.user_preferred

    # ------------------------------------------------------------------
    # Source selection
    # ------------------------------------------------------------------

    def _on_double_click(
        self,
        _event: tk.Event,
    ) -> None:
        self.toggle_selected_source()

    def _on_return(
        self,
        _event: tk.Event,
    ) -> None:
        self.toggle_selected_source()

    def toggle_selected_source(self) -> None:
        selection = self.tree.selection()

        if not selection:
            return

        item = selection[0]

        if item not in self.source_items:
            return

        resolution, source = self.source_items[item]

        key = (
            resolution.type_id,
            source.source_file_id,
        )

        current = self._effective_preferred(
            resolution.type_id,
            source,
        )

        self.pending_changes[key] = not current

        self.refresh_tree()

    # ------------------------------------------------------------------
    # Manual relation
    # ------------------------------------------------------------------

    def add_manual_relation(self) -> None:
        selection = self.tree.selection()

        if not selection:
            messagebox.showinfo(
                "Select type",
                "Select a type first.",
            )
            return

        item = selection[0]

        # If a source is selected, use its parent type.
        if item in self.source_items:
            resolution, _ = self.source_items[item]
        elif item in self.type_items:
            resolution = self.type_items[item]
        else:
            messagebox.showinfo(
                "Select type",
                "Select a type first.",
            )
            return

        ManualRelationWindow(
            parent=self,
            db=self.db,
            resolution=resolution,
            on_added=self._manual_relation_added,
        )

    def _manual_relation_added(self) -> None:
        self.reload()

    # ------------------------------------------------------------------
    # Save
    # ------------------------------------------------------------------

    def save(self) -> None:
        if not self.pending_changes:
            messagebox.showinfo(
                "Save",
                "There are no changes to save.",
            )
            return

        try:
            self.db.save_preferred_changes(
                self.pending_changes,
            )
        except sqlite3.Error as exc:
            messagebox.showerror(
                "Database error",
                f"Could not save changes:\n\n{exc}",
            )
            return

        self.pending_changes.clear()

        try:
            self.resolutions = self.db.load_resolutions()
        except sqlite3.Error as exc:
            messagebox.showerror(
                "Database error",
                f"Changes were saved, but reload failed:\n\n{exc}",
            )
            return

        self.refresh_tree()

        messagebox.showinfo(
            "Save",
            "User-preferred changes saved.",
        )

    # ------------------------------------------------------------------
    # Closing
    # ------------------------------------------------------------------

    def _on_close(self) -> None:
        if self.pending_changes:
            answer = messagebox.askyesnocancel(
                "Unsaved changes",
                "There are unsaved changes.\n\n"
                "Save them before closing?",
            )

            if answer is None:
                return

            if answer:
                self.save()

                if self.pending_changes:
                    return

        self.db.close()
        self.destroy()

    def _update_pending_label(self) -> None:
        count = len(self.pending_changes)

        if count:
            self.pending_label.configure(
                text=f"Unsaved changes: {count}",
            )
        else:
            self.pending_label.configure(
                text="No unsaved changes",
            )


# ---------------------------------------------------------------------------
# Manual relation window
# ---------------------------------------------------------------------------

class ManualRelationWindow(tk.Toplevel):
    """Search and manually attach a source file to a type."""

    def __init__(
        self,
        parent: ResolutionUI,
        db: ResolutionDatabase,
        resolution: TypeResolution,
        on_added,
    ):
        super().__init__(parent)

        self.parent_ui = parent
        self.db = db
        self.resolution = resolution
        self.on_added = on_added

        self.title(
            f"Add source — {resolution.name}"
        )
        self.geometry("800x550")
        self.minsize(600, 400)

        self.search_var = tk.StringVar(value=resolution.name)

        self._create_widgets()
        self.refresh_results()

        self.entry.focus_set()

        self.transient(parent)
        self.grab_set()

    def _create_widgets(self) -> None:
        top = ttk.Frame(
            self,
            padding=8,
        )
        top.pack(fill=tk.X)

        ttk.Label(
            top,
            text="Search:",
        ).pack(side=tk.LEFT)

        self.entry = ttk.Entry(
            top,
            textvariable=self.search_var,
        )
        self.entry.pack(
            side=tk.LEFT,
            fill=tk.X,
            expand=True,
            padx=8,
        )

        self.entry.bind(
            "<KeyRelease>",
            lambda _: self.refresh_results(),
        )

        ttk.Button(
            top,
            text="Search",
            command=self.refresh_results,
        ).pack(side=tk.RIGHT)

        frame = ttk.Frame(self)
        frame.pack(
            fill=tk.BOTH,
            expand=True,
            padx=8,
        )

        self.listbox = tk.Listbox(
            frame,
            selectmode=tk.SINGLE,
            font=("TkFixedFont", 10),
        )

        scrollbar = ttk.Scrollbar(
            frame,
            orient=tk.VERTICAL,
            command=self.listbox.yview,
        )

        self.listbox.configure(
            yscrollcommand=scrollbar.set,
        )

        self.listbox.pack(
            side=tk.LEFT,
            fill=tk.BOTH,
            expand=True,
        )

        scrollbar.pack(
            side=tk.RIGHT,
            fill=tk.Y,
        )

        self.paths: list[tuple[int, str]] = []

        self.listbox.bind(
            "<Double-1>",
            lambda _: self.add_selected(),
        )

        bottom = ttk.Frame(
            self,
            padding=8,
        )
        bottom.pack(fill=tk.X)

        ttk.Label(
            bottom,
            text=(
                "The relation will be created with "
                "score=0 and user_preferred=1."
            ),
        ).pack(side=tk.LEFT)

        ttk.Button(
            bottom,
            text="Cancel",
            command=self.destroy,
        ).pack(
            side=tk.RIGHT,
            padx=(8, 0),
        )

        ttk.Button(
            bottom,
            text="Add",
            command=self.add_selected,
        ).pack(side=tk.RIGHT)

    def refresh_results(self) -> None:
        query = self.search_var.get().strip()

        try:
            self.paths = self.db.search_paths(query)
        except sqlite3.Error as exc:
            messagebox.showerror(
                "Database error",
                str(exc),
                parent=self,
            )
            return

        self.listbox.delete(0, tk.END)

        for _, path in self.paths:
            self.listbox.insert(
                tk.END,
                path,
            )

    def add_selected(self) -> None:
        selection = self.listbox.curselection()

        if not selection:
            messagebox.showinfo(
                "Select file",
                "Select a source file first.",
                parent=self,
            )
            return

        index = selection[0]
        source_file_id, path = self.paths[index]

        try:
            self.db.add_manual_relation(
                type_id=self.resolution.type_id,
                source_file_id=source_file_id,
            )
        except sqlite3.Error as exc:
            messagebox.showerror(
                "Database error",
                f"Could not add relation:\n\n{exc}",
                parent=self,
            )
            return

        messagebox.showinfo(
            "Relation added",
            f"Added:\n\n{self.resolution.name}\n→ {path}",
            parent=self,
        )

        self.on_added()
        self.destroy()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="GUI for reviewing type-source resolution.",
    )

    parser.add_argument(
        "--db",
        required=True,
        help="Path to the SQLite database.",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if not Path(args.db).exists():
        print(
            f"Error: database not found: {args.db}",
            file=sys.stderr,
        )
        return 1

    try:
        app = ResolutionUI(args.db)
        app.mainloop()
    except sqlite3.Error as exc:
        print(
            f"Database error: {exc}",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())