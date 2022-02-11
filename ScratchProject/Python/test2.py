import bisect
import ctypes
import dataclasses
import io
import os
import pathlib
import pickle
import sys
import time
import typing
import zlib

from sqexdata import BlockHeader


class ZiPatchHeader(ctypes.BigEndianStructure):
    SIGNATURE = b"\x91\x5A\x49\x50\x41\x54\x43\x48\x0d\x0a\x1a\x0a"

    _fields_ = (
        ("signature", ctypes.c_char * 12),
    )

    signature: int | ctypes.c_char * 12


class ZiPatchChunkHeader(ctypes.BigEndianStructure):
    _fields_ = (
        ("size", ctypes.c_uint32),
        ("type", ctypes.c_char * 4),
    )

    size: int | ctypes.c_uint32
    type: bytes | ctypes.c_char * 4


class ZiPatchChunkFooter(ctypes.BigEndianStructure):
    _fields_ = (
        ("crc32", ctypes.c_uint32),
    )

    crc32: int | ctypes.c_uint32


class ZiPatchSqpackHeader(ctypes.BigEndianStructure):
    _fields_ = (
        ("size", ctypes.c_uint32),
        ("command", ctypes.c_char * 4),
    )

    size: int | ctypes.c_uint32
    command: bytes | ctypes.c_char * 4


class ZiPatchSqpackFileAddHeader(ctypes.BigEndianStructure):
    COMMAND = b'FA'

    _fields_ = (
        ("offset", ctypes.c_uint64),
        ("size", ctypes.c_uint64),
        ("path_size", ctypes.c_uint32),
        ("expac_id", ctypes.c_uint16),
        ("padding1", ctypes.c_uint16),
    )

    offset: int | ctypes.c_uint16
    size: int | ctypes.c_uint64
    path_size: int | ctypes.c_uint32
    expac_id: int | ctypes.c_uint16
    padding1: int | ctypes.c_uint16


class ZiPatchSqpackFileDeleteHeader(ctypes.BigEndianStructure):
    COMMAND = b'FD'

    _fields_ = (
        ("offset", ctypes.c_uint64),
        ("size", ctypes.c_uint64),
        ("path_size", ctypes.c_uint32),
        ("expac_id", ctypes.c_uint16),
        ("padding1", ctypes.c_uint16),
    )

    offset: int | ctypes.c_uint16
    size: int | ctypes.c_uint64
    path_size: int | ctypes.c_uint32
    expac_id: int | ctypes.c_uint16
    padding1: int | ctypes.c_uint16


class ZiPatchSqpackFileResolver(ctypes.BigEndianStructure):
    _fields_ = (
        ("main_id", ctypes.c_uint16),
        ("sub_id", ctypes.c_uint16),
        ("file_id", ctypes.c_uint32),
    )

    main_id: int | ctypes.c_uint16
    sub_id: int | ctypes.c_uint16
    file_id: int | ctypes.c_uint32

    @property
    def expac_id(self):
        return self.sub_id >> 8

    @property
    def path(self):
        if self.expac_id == 0:
            return f"sqpack/ffxiv/{self.main_id:02x}{self.sub_id:04x}.win32"
        else:
            return f"sqpack/ex{self.expac_id}/{self.main_id:02x}{self.sub_id:04x}.win32"


class ZiPatchSqpackAddData(ZiPatchSqpackFileResolver):
    COMMAND = b'A'

    _fields_ = (
        ("block_offset_value", ctypes.c_uint32),
        ("block_size_value", ctypes.c_uint32),
        ("clear_size_value", ctypes.c_uint32),
    )

    @property
    def block_offset(self):
        return self.block_offset_value * 128

    @property
    def block_size(self):
        return self.block_size_value * 128

    @property
    def clear_size(self):
        return self.clear_size_value * 128

    @property
    def path(self):
        return super().path + f".dat{self.file_id}"


class ZiPatchSqpackZeroData(ZiPatchSqpackFileResolver):
    COMMANDS = {b'E', b'D'}

    _fields_ = (
        ("block_offset_value", ctypes.c_uint32),
        ("block_size_value", ctypes.c_uint32),
    )

    @property
    def block_offset(self):
        return self.block_offset_value * 128

    @property
    def block_size(self):
        return self.block_size_value * 128

    @property
    def path(self):
        return super().path + f".dat{self.file_id}"


@dataclasses.dataclass
class FilePart:
    target_offset: int
    target_size: int

    source_path_index: typing.Optional[int] = None
    source_offset: int = 0
    source_size: int = 0
    source_is_block: bool = False
    split_from: int = 0


def replace_block(d: typing.List[FilePart], part: FilePart):
    for split_offset in {part.target_offset, part.target_offset + part.target_size}:
        i = bisect.bisect_left(d, split_offset, key=lambda x: x.target_offset)
        prev_end_offset = d[i - 1].target_offset + d[i - 1].target_size if d else 0
        if i == len(d) and split_offset == prev_end_offset:
            continue
        if i == len(d) and split_offset > prev_end_offset:
            d.append(FilePart(target_offset=prev_end_offset, target_size=split_offset - prev_end_offset))
            continue
        if i < len(d) and d[i].target_offset == split_offset:
            continue
        if d and i == 0:
            if split_offset == 0:
                d.insert(0, FilePart(target_offset=0, target_size=d[0].target_offset))
            else:
                d.insert(0, FilePart(target_offset=0, target_size=split_offset))
                d.insert(1, FilePart(target_offset=split_offset, target_size=d[1].target_offset - split_offset))
        else:
            i -= 1
            if d[i].target_offset + d[i].target_size - split_offset < 0:
                breakpoint()
            if split_offset - d[i].target_offset < 0:
                breakpoint()
            d.insert(i + 1, FilePart(
                target_offset=split_offset,
                target_size=d[i].target_offset + d[i].target_size - split_offset,
                split_from=d[i].split_from + split_offset - d[i].target_offset,
                source_path_index=d[i].source_path_index,
                source_offset=d[i].source_offset,
                source_size=d[i].source_size,
                source_is_block=d[i].source_is_block,
            ))
            d[i].target_size = split_offset - d[i].target_offset

    i = bisect.bisect_left(d, part.target_offset, key=lambda x: x.target_offset)
    j = bisect.bisect_left(d, part.target_offset + part.target_size, key=lambda x: x.target_offset)
    if i >= len(d) or d[i].target_offset != part.target_offset:
        raise RuntimeError("left")
    if j < len(d) and d[j].target_offset != part.target_offset + part.target_size:
        raise RuntimeError("right(1)")
    elif j == len(d) and d[j - 1].target_offset + d[j - 1].target_size != part.target_offset + part.target_size:
        raise RuntimeError("right(2)")

    d[i] = part
    del d[i + 1:j]

    pass


def __main__():
    source_files = []
    recreated = {}
    patch_source_path = pathlib.Path(r"Z:\patch-dl.ffxiv.com\game\4e9a232b")
    last_intermediate = None

    for f in sorted((x for x in patch_source_path.iterdir() if x.suffix == '.patch'), key=lambda x: x.name[1:]):
        print(f)
        next_print_update = time.time() + 0.2
        intermediate = f.with_suffix(".progress")
        if intermediate.exists():
            last_intermediate = intermediate
            continue
        if last_intermediate is not None:
            with last_intermediate.open("rb") as fp:
                source_files = pickle.load(fp)
                recreated = pickle.load(fp)
            last_intermediate = None

        source_files.append(f.name)

        with f.open("rb") as fp:
            fp.seek(0, os.SEEK_END)
            flen = fp.tell()

            fp.seek(0, os.SEEK_SET)
            fp: typing.BinaryIO | io.BytesIO
            fp.readinto(hdr := ZiPatchHeader())
            if hdr.signature != ZiPatchHeader.SIGNATURE:
                raise RuntimeError

            while fp.readinto(hdr := ZiPatchChunkHeader()):
                offset = fp.tell()
                if time.time() >= next_print_update:
                    print(f"{offset / 1048576:.02f}/{flen / 1048576:.02f} ({offset / flen * 100:.02f}%)", end="\r")
                    next_print_update = time.time() + 0.2
                    sys.stdout.flush()
                if hdr.type == b"SQPK":
                    fp.readinto(sqpkhdr := ZiPatchSqpackHeader())
                    if sqpkhdr.command in (b"T", b"X"):
                        pass

                    elif sqpkhdr.command == ZiPatchSqpackFileAddHeader.COMMAND:
                        fp.readinto(sqpkhdr2 := ZiPatchSqpackFileAddHeader())
                        path = fp.read(sqpkhdr2.path_size).split(b"\0", 1)[0].decode("utf-8")
                        if path not in recreated:
                            recreated[path] = []

                        current_file_offset = sqpkhdr2.offset
                        while fp.tell() < offset + hdr.size:
                            block_offset = fp.tell()
                            fp.readinto(block_header := BlockHeader())
                            block_data_size = block_header.compressed_size if block_header.is_compressed() else block_header.decompressed_size
                            padded_block_size = (block_data_size + ctypes.sizeof(block_header) + 127) & 0xFFFFFF80
                            fp.seek(padded_block_size - ctypes.sizeof(block_header), os.SEEK_CUR)
                            replace_block(recreated[path], FilePart(
                                source_path_index=len(source_files) - 1,
                                source_offset=block_offset,
                                source_size=padded_block_size,
                                source_is_block=True,
                                split_from=0,
                                target_offset=current_file_offset,
                                target_size=block_header.decompressed_size,
                            ))
                            current_file_offset += block_header.decompressed_size

                    elif sqpkhdr.command == ZiPatchSqpackFileDeleteHeader.COMMAND:
                        fp.readinto(sqpkhdr2 := ZiPatchSqpackFileDeleteHeader())
                        path = fp.read(sqpkhdr2.path_size).split(b"\0", 1)[0].decode("utf-8")
                        recreated.pop(path, None)

                    elif sqpkhdr.command == ZiPatchSqpackAddData.COMMAND:
                        fp.readinto(sqpkhdr2 := ZiPatchSqpackAddData())
                        path = sqpkhdr2.path
                        if path not in recreated:
                            recreated[path] = []

                        replace_block(recreated[path], FilePart(
                            source_path_index=len(source_files) - 1,
                            source_offset=fp.tell(),
                            source_size=sqpkhdr2.block_size,
                            source_is_block=False,
                            split_from=0,
                            target_offset=sqpkhdr2.block_offset,
                            target_size=sqpkhdr2.block_size,
                        ))
                        if sqpkhdr2.clear_size:
                            replace_block(recreated[path], FilePart(
                                source_path_index=None,
                                source_offset=0,
                                source_size=0,
                                source_is_block=False,
                                split_from=0,
                                target_offset=sqpkhdr2.block_offset + sqpkhdr2.block_size,
                                target_size=sqpkhdr2.clear_size,
                            ))

                    elif sqpkhdr.command in ZiPatchSqpackZeroData.COMMANDS:
                        fp.readinto(sqpkhdr2 := ZiPatchSqpackZeroData())
                        path = sqpkhdr2.path
                        if path not in recreated:
                            recreated[path] = []

                        replace_block(recreated[path], FilePart(
                            source_path_index=None,
                            source_offset=0,
                            source_size=0,
                            source_is_block=False,
                            split_from=0,
                            target_offset=sqpkhdr2.block_offset,
                            target_size=sqpkhdr2.block_size,
                        ))

                    elif sqpkhdr.command in {b'HDV', b'HDI', b'HDD', b'HIV', b'HII', b'HID'}:
                        fp.readinto(sqpkhdr2 := ZiPatchSqpackFileResolver())
                        path = sqpkhdr2.path
                        if sqpkhdr.command[1:2] == b'D':
                            path += f".dat{sqpkhdr2.file_id}"
                        elif sqpkhdr.command[1:2] == b'I':
                            path += f".index"
                        else:
                            raise RuntimeError

                        header_offset = 0
                        if sqpkhdr.command[2:3] in b'DI':
                            header_offset = 1024
                        elif sqpkhdr.command[2:3] == b'V':
                            pass
                        else:
                            raise RuntimeError
                        if path not in recreated:
                            recreated[path] = []

                        replace_block(recreated[path], FilePart(
                            source_path_index=len(source_files) - 1,
                            source_offset=fp.tell(),
                            source_size=1024,
                            source_is_block=False,
                            split_from=0,
                            target_offset=header_offset,
                            target_size=1024,
                        ))

                fp.seek(offset + hdr.size, os.SEEK_SET)
                fp.readinto(ftr := ZiPatchChunkFooter())
                if hdr.type == b"EOF_":
                    break
        with intermediate.with_suffix(".progresstmp").open("wb") as fp:
            pickle.dump(source_files, fp)
            pickle.dump(recreated, fp)
        intermediate.with_suffix(".progresstmp").rename(intermediate)
    if last_intermediate is not None:
        with last_intermediate.open("rb") as fp:
            source_files = pickle.load(fp)
            recreated = pickle.load(fp)
        last_intermediate = None

    files = [(patch_source_path / x).open("rb") for x in source_files]

    for path, parts in recreated.items():
        parts: typing.Set[FilePart]
        target_path = pathlib.Path(r"Z:\xivrtest") / path
        target_path.parent.mkdir(parents=True, exist_ok=True)
        print(target_path)
        with target_path.open("wb+") as fp:
            for part in parts:
                fp.seek(part.target_offset)
                if part.source_path_index is None:
                    continue  # todo: zero fill
                files[part.source_path_index].seek(part.source_offset)
                data = files[part.source_path_index].read(part.source_size)
                if part.source_is_block:
                    block = BlockHeader.from_buffer_copy(data)
                    if block.is_compressed():
                        data = zlib.decompress(data[ctypes.sizeof(block):ctypes.sizeof(block) + block.compressed_size],
                                               -zlib.MAX_WBITS)
                    else:
                        data = data[ctypes.sizeof(block):ctypes.sizeof(block) + block.decompressed_size]
                data = data[part.split_from:part.split_from + part.target_size]
                fp.write(data)

    return 0


if __name__ == "__main__":
    exit(__main__())
