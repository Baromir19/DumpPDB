"""MSF 7.00 container (the file format underneath a ``.pdb``).

A PDB is a miniature file system: fixed-size blocks, a "stream directory" listing
the blocks of every stream, and a block map that locates the directory itself.
This module reads it completely independently of the DIA SDK.
"""

from __future__ import annotations

import struct
from pathlib import Path

MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\x00\x00\x00"


class PdbFormatError(ValueError):
    pass


class Msf:
    """Random access to the streams of an MSF 7.00 file."""

    def __init__(self, path: str | Path):
        self.path = Path(path)
        self._data = self.path.read_bytes()
        d = self._data
        if d[:len(MAGIC)] != MAGIC:
            raise PdbFormatError("not an MSF 7.00 (PDB 7) file: " + str(path))

        (self.block_size, self.free_block_map, self.num_blocks,
         self.dir_size, _unknown, self.block_map_addr) = struct.unpack_from("<IIIIII", d, 32)
        if self.block_size not in (512, 1024, 2048, 4096):
            raise PdbFormatError(f"unsupported block size {self.block_size}")
        if self.num_blocks * self.block_size != len(d):
            raise PdbFormatError("file size does not match the block count (truncated file?)")

        self._streams: list[tuple[int, list[int]]] = self._read_directory()

    # -- directory ---------------------------------------------------------------

    def _blocks_bytes(self, blocks: list[int]) -> bytes:
        bs = self.block_size
        d = self._data
        return b"".join(d[b * bs:(b + 1) * bs] for b in blocks)

    def _read_directory(self) -> list[tuple[int, list[int]]]:
        bs = self.block_size
        n_dir_blocks = -(-self.dir_size // bs)
        # The block map lists the blocks that hold the directory's block list.
        n_map_blocks = -(-(n_dir_blocks * 4) // bs)
        map_raw = self._data[self.block_map_addr * bs:
                             self.block_map_addr * bs + n_map_blocks * bs] \
            if n_map_blocks == 1 else None
        if map_raw is None:
            # The block map itself may span several consecutive blocks.
            map_raw = self._data[self.block_map_addr * bs:(self.block_map_addr + n_map_blocks) * bs]
        dir_blocks = list(struct.unpack_from(f"<{n_dir_blocks}I", map_raw, 0))
        raw = self._blocks_bytes(dir_blocks)[:self.dir_size]

        (n_streams,) = struct.unpack_from("<I", raw, 0)
        sizes = list(struct.unpack_from(f"<{n_streams}I", raw, 4))
        pos = 4 + 4 * n_streams
        streams: list[tuple[int, list[int]]] = []
        for size in sizes:
            if size == 0xFFFFFFFF:  # deleted stream
                streams.append((0, []))
                continue
            n = -(-size // bs)
            blocks = list(struct.unpack_from(f"<{n}I", raw, pos)) if n else []
            pos += 4 * n
            streams.append((size, blocks))
        return streams

    # -- public API ------------------------------------------------------------------

    @property
    def stream_count(self) -> int:
        return len(self._streams)

    def stream_size(self, index: int) -> int:
        return self._streams[index][0]

    def stream(self, index: int) -> bytes:
        """Whole content of a stream (empty for a missing/deleted one)."""
        if index < 0 or index >= len(self._streams):
            return b""
        size, blocks = self._streams[index]
        return self._blocks_bytes(blocks)[:size]
