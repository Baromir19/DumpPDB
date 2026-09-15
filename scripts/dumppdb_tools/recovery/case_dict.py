"""Case-variant dictionary with scoring."""


def score(value: str) -> int:
    """Score a string by its case pattern.

    Higher score means the casing is more "specific" (more uppercase
    letters, longer length). Used to pick the best case variant.
    """
    upper = sum(c.isupper() for c in value)
    lower = sum(c.islower() for c in value)

    if upper == 0:
        return 0

    if lower == 0:
        return 1

    return upper * 10 + len(value)


class CaseDict:
    """A case-insensitive dictionary that keeps the best-scoring variant.

    Keys are lowercased; values are ``(original, score)`` tuples.
    """

    def __init__(self):
        self._storage: dict[str, tuple[str, int]] = {}

    def add(self, value: str) -> None:
        """Add a value, keeping the variant with the highest score."""
        key = value.lower()
        new_score = score(value)
        old = self._storage.get(key)

        if old is None or new_score > old[1]:
            self._storage[key] = (value, new_score)

    def get(self, key: str) -> str | None:
        """Return the best-scoring variant for a case-insensitive key."""
        entry = self._storage.get(key.lower())

        if entry is None:
            return None

        return entry[0]

    def __contains__(self, key: str) -> bool:
        return key.lower() in self._storage

    def __len__(self) -> int:
        return len(self._storage)

    def items(self):
        return self._storage.items()