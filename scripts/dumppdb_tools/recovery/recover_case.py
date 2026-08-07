"""Case recovery from executable strings and PDB symbols."""

from dumppdb_tools.models import SourcePath
from dumppdb_tools.pdb_client import PdbClient
from dumppdb_tools.recovery.case_dict import CaseDict
from dumppdb_tools.recovery.path_normalizer import CODE_EXTENSIONS, remove_extension


def build_case_dictionary(pdb: PdbClient, exe_path: str) -> CaseDict:
    """Build a case dictionary from executable strings and PDB symbols.

    Collects case variants from:

    * String literals found in the executable (paths with known source
      extensions).
    * Symbol names enumerated from the PDB.

    Returns a :class:`CaseDict` mapping lowercased names to the
    best-scoring case variant.
    """
    case_dict = CaseDict()

    exe_raw_strings = pdb.find_strings(
        exe_path,
        min_length=5,
        encodings=1 | 2 | 4,
        section_names="",
        regex_pattern="",
    )

    for line in exe_raw_strings.splitlines():
        path = line.strip()

        if not path.lower().endswith(tuple(CODE_EXTENSIONS)):
            continue

        path = path.replace("\\", "/")

        for part in path.split("/"):
            if not part:
                continue

            part = remove_extension(part)
            case_dict.add(part)

    symbols = pdb.enumerate_symbols()

    for symbol in symbols.splitlines():
        symbol = symbol.strip()

        if symbol:
            case_dict.add(symbol)

    return case_dict


def recover_case(paths: list[SourcePath], case_dict: CaseDict) -> None:
    """Apply the best-scoring case variant to each path component.

    Mutates ``paths`` in place.
    """
    for path in paths:
        parts = path.normalized.split("/")

        for i, part in enumerate(parts):
            candidate = case_dict.get(part)

            if candidate:
                parts[i] = candidate

        path.normalized = "/".join(parts)