#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build an FM-1 .fwsc update image.

Python implementation of the FM-1 update layout. The boot resources in
sdk/tools/pack are used unchanged; this script only reproduces the
container format and checksums.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

AREA = 0x4000
ENTRY_POINT = 0x02000120
APP_DATA = 0x120
FLASH_LIMIT = 0xFF000
USER_SIZE = 536576
BT_SIZE = 4096
MINIMUM_VM = 32768
PUBLIC_CODE = 0x980F

CRC_TABLE = []
for _i in range(256):
    _value = _i << 8
    for _bit in range(8):
        _value = ((_value << 1) ^ (0 if _value & 0x8000 == 0 else 0x1021)) & 0xFFFF
    CRC_TABLE.append(_value)


def crc16(data: bytes | bytearray, begin: int = 0, length: int | None = None) -> int:
    if length is None:
        length = len(data) - begin
    value = 0
    for index in range(begin, begin + length):
        value = ((value << 8) ^ CRC_TABLE[((value >> 8) ^ data[index]) & 0xFF]) & 0xFFFF
    return value


def cipher(data: bytearray, begin: int, length: int, state: int) -> None:
    for index in range(begin, begin + length):
        data[index] ^= state & 0xFF
        state = ((state << 1) ^ (0 if state & 0x8000 == 0 else 0x1021)) & 0xFFFF


def align(size: int, boundary: int) -> int:
    return (size + boundary - 1) & -boundary


def put_number(data: bytearray, offset: int, value: int, width: int) -> None:
    for index in range(width):
        data[offset + index] = (value >> (8 * index)) & 0xFF


def put_text(data: bytearray, offset: int, text: str) -> None:
    raw = text.encode("ascii")
    data[offset:offset + len(raw)] = raw


def record(data: bytearray, offset: int, name: str, checksum: int,
           address: int, size: int, attributes: int, last: int,
           extra: int | None) -> None:
    data[offset:offset + 32] = b"\0" * 32
    put_number(data, offset + 2, checksum, 2)
    put_number(data, offset + 4, address, 4)
    put_number(data, offset + 8, size, 4)
    put_number(data, offset + 12, attributes, 2)
    put_number(data, offset + 14, last, 2)
    data[offset + 16:offset + 32] = b"\xff" * 16
    put_text(data, offset + 16, name)
    data[offset + 16 + len(name)] = 0
    if extra is not None:
        put_number(data, offset + 28, extra, 4)
    put_number(data, offset, crc16(data, offset + 2, 30), 2)


def read_resource(directory: Path, name: str, size: int) -> bytes:
    data = (directory / name).read_bytes()
    if len(data) != size:
        raise ValueError(f"unexpected FM-1 resource size for {name}: {len(data)}")
    return data


def encode_boot(plain: bytes) -> bytes:
    """XOR the uboot bank directory and payload with the fixed cipher."""
    result = bytearray(plain)
    banks = struct.unpack_from("<H", plain, 0)[0]
    if banks < 1 or banks > len(plain) // 16:
        raise ValueError("invalid boot directory")
    previous_end = banks * 16
    for bank in range(banks):
        header = bank * 16
        length = struct.unpack_from("<H", plain, header + 2)[0]
        start = struct.unpack_from("<i", plain, header + 8)[0]
        if (length == 0 or start < previous_end or start > len(plain) - length
                or crc16(plain, header, 14) != struct.unpack_from("<H", plain, header + 14)[0]
                or crc16(plain, start, length) != struct.unpack_from("<H", plain, header + 12)[0]):
            raise ValueError("invalid boot bank")
        cipher(result, header, 16, 0xFFFF)
        cipher(result, start, length, 0xFFFF)
        previous_end = start + length
    return bytes(result)


def build_flash(application: bytes, resources: Path, end: int, vm_end: int) -> bytearray:
    boot = read_resource(resources, "uboot.boot", 14488)
    config = read_resource(resources, "boot-config.bin", 721)
    tool = read_resource(resources, "cfg_tool.bin", 383)

    image = bytearray(b"\xff" * end)
    settings = bytearray(34 + len(config))
    # Public chip-key descriptor: one bit per key byte, deterministic.
    for bit in range(16):
        settings[bit] = 16
        settings[16 + bit] = 16 ^ (255 if (PUBLIC_CODE >> bit) & 1 == 0 else 0)
    put_number(settings, 32, crc16(settings, 0, 32), 2)
    settings[34:34 + len(config)] = config

    # AC791N boot header fields taken from the verified FM-1 boot image.
    header = bytes([0, 0, 0x20, 2, 0x2F, 0x10, 0x4C, 0xC9, 0, 0xF0, 0x0F, 0,
                    0x10, 0x10, 0xA4, 0xFF, 0x4E, 0x5D, 0x0B, 0x41, 0xE0, 0xCD,
                    0x78, 0x1D, 0xC8, 0x56, 0x41, 0x7F, 0xB3, 0x67, 0xCF, 0x9F])
    image[0:32] = header
    put_number(image, 0, crc16(image, 2, 30), 2)

    config_at = 160 + len(boot)
    record(image, 32, "uboot.boot", crc16(boot), 160, len(boot), 0, 0, None)
    record(image, 64, "isd_config.ini", crc16(settings), config_at,
           len(settings), 0x8002, 0, None)
    record(image, 96, "app_dir_head", 0xFFFF, AREA, 0xFFFFFFFF, 0xFF81, 0, None)
    record(image, 128, "key_mac", 0xFFFF, FLASH_LIMIT, 4096, 0x0112, 0xFFFF, None)

    for offset in range(0, 160, 32):
        cipher(image, offset, 32, 0xFFFF)
    image[160:160 + len(boot)] = encode_boot(boot)
    image[config_at:config_at + len(settings)] = settings

    payload_size = APP_DATA + len(application) + len(tool)
    app_at = AREA + APP_DATA
    image[app_at:app_at + len(application)] = application
    image[app_at + len(application):app_at + len(application) + len(tool)] = tool

    record(image, AREA + 32, "app.bin", crc16(application), APP_DATA,
           len(application), 0xFF82, 0, None)
    record(image, AREA + 64, "cfg_tool.bin", crc16(tool),
           APP_DATA + len(application), len(tool), 0xFF82, 0, None)
    record(image, AREA + 96, "VM", 0xFFFF, end, vm_end - end, 0x8112, 0,
           MINIMUM_VM)
    record(image, AREA + 128, "PRCT", 0xFFFF, 0, end, 0x8292, 0, 0x45444F43)
    record(image, AREA + 160, "BTIF", 0xFFFF, vm_end, BT_SIZE, 0x8192, 1,
           0x4F545541)
    record(image, AREA, "app_area_head", crc16(image, AREA + 32, payload_size - 32),
           ENTRY_POINT, payload_size, 0xFF83, 0, None)

    reserved = AREA + payload_size
    record(image, reserved + 32, "USER", 0xFFFF, FLASH_LIMIT - USER_SIZE,
           USER_SIZE, 0x8192, 1, 0x4F545541)
    record(image, reserved, "EXT_RESERVED", crc16(image, reserved + 32, 32),
           32, 64, 0xFF93, 1, None)

    for offset in range(0, payload_size + 64, 32):
        length = min(32, payload_size + 64 - offset)
        cipher(image, AREA + offset, length, PUBLIC_CODE ^ (offset >> 2))
    return image


def wrap(flash: bytearray, loader: bytes, product: str) -> bytes:
    if not 1 <= len(loader) < 32768:
        raise ValueError("invalid FM-1 update loader")
    loader_at = 1024 + align(len(flash), 32)
    total = loader_at + align(len(loader), 32)
    ufw = bytearray(b"\xff" * total)
    ufw[0:224] = b"\0" * 224

    for index, (payload, name) in enumerate(((flash, "flash.bin"), (loader, "ota.bin"))):
        entry = 64 + index * 80
        put_number(ufw, entry, 0 if index == 0 else 100, 2)
        put_number(ufw, entry + 2, index, 2)
        put_number(ufw, entry + 4, crc16(payload), 2)
        put_number(ufw, entry + 8, 1024 if index == 0 else loader_at, 4)
        put_number(ufw, entry + 12, len(payload), 4)
        put_number(ufw, entry + 16, align(len(payload), 32), 4)
        put_text(ufw, entry + 64, name)
        cipher(ufw, entry, 80, 0xFFFF)

    put_number(ufw, 4, total, 4)
    put_number(ufw, 8, 2, 2)
    put_number(ufw, 10, 4, 2)
    put_number(ufw, 12, 512, 4)
    put_text(ufw, 16, "AC791N")
    put_number(ufw, 2, crc16(ufw, 64, 160), 2)
    put_number(ufw, 0, crc16(ufw, 2, 62), 2)
    cipher(ufw, 0, 64, 0xFFFF)

    ufw[1024:1024 + len(flash)] = flash
    ufw[loader_at:loader_at + len(loader)] = loader

    # The product string is interleaved into the header so the updater can
    # recognise the image before the payload is decrypted.
    output = bytearray(total + 20)
    destination = 0
    for source in range(total):
        output[destination] = ufw[source]
        destination += 1
        if source < 940 and (source + 1) % 47 == 0:
            index = source // 47
            output[destination] = (ord(product[index]) + index + 1) & 0xFF \
                if index < len(product) else 125
            destination += 1
    return bytes(output)


def create(application: bytes, resources: Path, output: Path, user_bytes: int,
           product: str) -> dict[str, int | str | bool]:
    if not re.fullmatch(r"FM-1_8[0-9]{6}", product):
        raise ValueError("invalid FM-1 update identity")
    if not application or len(application) > (
            FLASH_LIMIT - USER_SIZE - BT_SIZE - MINIMUM_VM - AREA - APP_DATA
            - 383 - 64):
        raise ValueError("application does not fit the FM-1 layout")
    if user_bytes != USER_SIZE:
        raise ValueError("unsupported FM-1 user partition size")

    image_size = align(AREA + APP_DATA + len(application) + 383 + 64, 4096)
    user_at = FLASH_LIMIT - USER_SIZE
    vm_end = user_at - BT_SIZE
    if image_size > vm_end - MINIMUM_VM:
        raise ValueError("insufficient FM-1 settings space")

    image = build_flash(application, resources, image_size, vm_end)
    loader = (resources / "ota.bin").read_bytes()
    package = wrap(image, loader, product)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(package)

    return {
        "flash_bytes": 1048576,
        "fits": True,
        "FLASH_BIN_SIZE": image_size,
        "FLASH_NEED_SIZE": FLASH_LIMIT,
        "VM_START_ADDR": image_size,
        "VM_END_ADDR": vm_end,
        "VM_REAL_SIZE": vm_end - image_size,
        "BTIF_RESERVED_START": vm_end,
        "BTIF_RESERVED_END": user_at,
        "USER_RESERVED_START": user_at,
        "USER_RESERVED_END": FLASH_LIMIT,
        "vm_minimum_bytes": MINIMUM_VM,
        "vm_extra_gap_bytes": vm_end - image_size - MINIMUM_VM,
        "btif_bytes": BT_SIZE,
        "user_bytes": USER_SIZE,
        "package_bytes": len(package),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="linked .text binary")
    parser.add_argument("-o", "--output", type=Path,
                        default=Path("build/fm1.fwsc"))
    parser.add_argument("--resources", type=Path,
                        default=Path("sdk/tools/pack"))
    parser.add_argument("--product", default="FM-1_8000014")
    parser.add_argument("--user-bytes", type=int, default=USER_SIZE)
    arguments = parser.parse_args()

    try:
        report = create(arguments.image.read_bytes(), arguments.resources,
                        arguments.output, arguments.user_bytes,
                        arguments.product)
    except (OSError, ValueError) as error:
        print(f"package: {error}", file=sys.stderr)
        return 1
    for key, value in report.items():
        print(f"{key}={value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
