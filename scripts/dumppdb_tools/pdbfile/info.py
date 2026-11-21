"""PDB info stream (stream 1): identity of the PDB and its named streams."""

from __future__ import annotations

import struct
from dataclasses import dataclass, field


@dataclass
class PdbInfo:
    version: int = 0
    signature: int = 0
    age: int = 0
    guid: str = ""
    named_streams: dict[str, int] = field(default_factory=dict)

    @property
    def guid_no_dashes(self) -> str:
        return self.guid.replace("-", "")


def _guid_text(raw: bytes) -> str:
    d1, d2, d3 = struct.unpack_from("<IHH", raw, 0)
    tail = raw[8:16]
    return f"{d1:08X}-{d2:04X}-{d3:04X}-{tail[:2].hex().upper()}-{tail[2:].hex().upper()}"


def parse_info(data: bytes) -> PdbInfo:
    info = PdbInfo()
    if len(data) < 28:
        return info
    info.version, info.signature, info.age = struct.unpack_from("<III", data, 0)
    info.guid = _guid_text(data[12:28])

    # Named stream map: string buffer + a hash table name-offset -> stream index.
    pos = 28
    (buf_size,) = struct.unpack_from("<I", data, pos)
    pos += 4
    strings = data[pos:pos + buf_size]
    pos += buf_size

    def read_bitvector(p):
        (n_words,) = struct.unpack_from("<I", data, p)
        p += 4
        words = struct.unpack_from(f"<{n_words}I", data, p)
        return words, p + 4 * n_words

    size, capacity = struct.unpack_from("<II", data, pos)
    pos += 8
    present, pos = read_bitvector(pos)
    _deleted, pos = read_bitvector(pos)

    def is_present(i):
        w = i // 32
        return w < len(present) and (present[w] >> (i % 32)) & 1

    for i in range(capacity):
        if not is_present(i):
            continue
        name_off, stream = struct.unpack_from("<II", data, pos)
        pos += 8
        end = strings.find(b"\0", name_off)
        info.named_streams[strings[name_off:end].decode("utf-8", "replace")] = stream
    return info
