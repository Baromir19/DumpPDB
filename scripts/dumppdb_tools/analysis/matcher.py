"""SymbolFileMatcher: runs the heuristic and writes symbol_file_candidates.

Algorithm
---------
For every symbol in the database:

1. Fetch the list of files that DIA linked to it
   (from *symbol_source_files*).
2. Score every file with :class:`~dumppdb_tools.analysis.scorer.TypeFileScorer`.
3. Keep the top-N candidates (default 10).
4. Write them to *symbol_file_candidates* via the repository.

The matcher also classifies each symbol:

* **resolved**   — the gap between best and second-best score is ≥ *gap_threshold*
* **ambiguous**  — multiple files have near-equal scores
* **no_match**   — every candidate scored ≤ 0
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable

from dumppdb_tools.analysis.scorer import TypeFileScorer
from dumppdb_tools.storage.repository import SymbolRepository


# Default number of top candidates stored per symbol
_DEFAULT_TOP_N = 10

# Score gap that distinguishes "resolved" from "ambiguous"
_DEFAULT_GAP_THRESHOLD = 30


@dataclass
class MatchResult:
    """Summary of all symbols processed by the matcher."""
    resolved:  list[tuple[str, str, int]] = field(default_factory=list)
    ambiguous: list[tuple[str, list[tuple[str, int]]]] = field(default_factory=list)
    no_match:  list[str] = field(default_factory=list)

    @property
    def total(self) -> int:
        return len(self.resolved) + len(self.ambiguous) + len(self.no_match)


class SymbolFileMatcher:
    """Scores and stores file candidates for every symbol.

    Parameters
    ----------
    repo:
        The repository to read from and write to.
    scorer:
        A pre-configured :class:`TypeFileScorer` instance.
    top_n:
        How many candidates to keep per symbol.
    gap_threshold:
        Minimum score difference between 1st and 2nd place to declare
        the result "resolved" rather than "ambiguous".
    progress_callback:
        Optional callable ``(current, total)`` for progress reporting.
    """

    def __init__(
        self,
        repo: SymbolRepository,
        scorer: TypeFileScorer,
        top_n: int = _DEFAULT_TOP_N,
        gap_threshold: int = _DEFAULT_GAP_THRESHOLD,
        progress_callback: Callable[[int, int], None] | None = None,
    ) -> None:
        self._repo     = repo
        self._scorer   = scorer
        self._top_n    = top_n
        self._gap      = gap_threshold
        self._progress = progress_callback

    # ------------------------------------------------------------------
    # Public interface
    # ------------------------------------------------------------------

    def run(self, clear_first: bool = True) -> MatchResult:
        """Process every symbol and populate *symbol_file_candidates*.

        Parameters
        ----------
        clear_first:
            When *True* (default), existing candidates are deleted before
            the run so results are always fresh.
        """
        if clear_first:
            self._repo.clear_candidates()

        result   = MatchResult()
        symbols  = list(self._repo.iter_symbols())
        total    = len(symbols)

        for idx, (symbol_id, symbol_name) in enumerate(symbols):
            self._process_symbol(symbol_id, symbol_name, result)

            if self._progress and (idx % 100 == 0 or idx == total - 1):
                self._progress(idx + 1, total)

        # Flush remaining inserts
        self._repo._db.commit()

        return result

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _process_symbol(
        self,
        symbol_id: int,
        symbol_name: str,
        result: MatchResult,
    ) -> None:
        files = self._repo.get_files_for_symbol(symbol_id)

        if not files:
            result.no_match.append(symbol_name)
            return

        # Score every file
        scored: list[tuple[int, str, int, str]] = []  # (score, path, file_id, reason)
        for file_id, file_path in files:
            score, reason = self._scorer.score(symbol_name, file_path, file_id)
            scored.append((score, file_path, file_id, reason))

        # Sort descending by score, keep top-N
        scored.sort(key=lambda x: x[0], reverse=True)
        top = scored[: self._top_n]

        # Write candidates
        for score, _path, file_id, reason in top:
            self._repo.upsert_candidate(symbol_id, file_id, score, reason)

        # Classify
        if not top or top[0][0] <= 0:
            result.no_match.append(symbol_name)
            return

        best_score   = top[0][0]
        best_path    = top[0][1]
        second_score = top[1][0] if len(top) > 1 else -1

        if best_score - second_score >= self._gap:
            result.resolved.append((symbol_name, best_path, best_score))
        else:
            # Collect all paths with score within gap of the best
            near_best = [
                (path, score)
                for score, path, _, _ in top
                if best_score - score < self._gap
            ]
            result.ambiguous.append((symbol_name, near_best))
