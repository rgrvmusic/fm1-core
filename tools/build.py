#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build an FM-1 firmware image with the JieLi PI32v2 toolchain.

The platform layer in platform/ and the application passed with --app are
compiled, linked against the fixed runtime in sdk/lib/libfm1_engine.a, and
optionally packaged as a .fwsc update image.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
PRODUCT_RE = re.compile(r"FM-1_8[0-9]{6}")
ENTRY_POINT = 0x02000120
USER_BYTES = 536576
UPDATE_FAMILY = 7

COMMON_FLAGS = [
    "-target", "pi32v2",
    "-integrated-as",
    "-mcpu=r3",
    "-mfprev1",
    "-Wuninitialized",
    "-Wno-invalid-noreturn",
    "-fno-common",
    "-fallow-pointer-null",
    "-fprefer-gnu-section",
    "-femulated-tls",
    "-Wno-shift-negative-value",
    "-Wframe-larger-than=2560",
    "-fms-extensions",
    "-fno-unwind-tables",
    "-ffunction-sections",
    "-fdata-sections",
    "-fmessage-length=0",
    "-cl-single-precision-constant",
    "-mllvm", "-pi32v2-large-program=true",
    "-D__GCC_PI32V2__",
    "-O2",
    "-fwrapv",
    "-fno-fast-math",
    "-ffp-contract=off",
]

# The scanner and the display driver address registers directly and run from
# interrupt/control context; keep them out of the PIC register machinery.
EXTRA_FLAGS = {
    "platform/panel.c": ["-fno-pic"],
    "platform/display.c": ["-fno-pic"],
}

LD_UNDEFINED = [
    "connectivity_wl82_bluetooth_start",
    "audio_app_bluetooth_open",
    "bt_get_local_name",
    "bt_get_mac_addr",
]


class BuildError(RuntimeError):
    pass


def run(command: list[str], *, capture: bool = False) -> subprocess.CompletedProcess:
    printable = " ".join(str(part) for part in command)
    if not capture:
        print(printable)
    result = subprocess.run(command, capture_output=capture, text=True)
    if result.returncode != 0:
        if capture:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
        raise BuildError(f"command failed ({result.returncode}): {command[0]}")
    return result


def find_toolchain(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        base = Path(explicit).expanduser()
        candidates += [base / "bin", base / "pi32v2" / "bin", base]
    for name in ("FM1_PI32_TOOLCHAIN", "JIELI_TOOLCHAIN"):
        value = os.environ.get(name)
        if value:
            base = Path(value).expanduser()
            candidates += [base / "bin", base / "pi32v2" / "bin", base]
    candidates += [
        Path.home() / ".jieli" / "toolchain" / "pi32v2" / "bin",
        Path.home() / ".jieli" / "toolchain" / "bin",
        Path.home() / ".jieli" / "toolchain" / "common" / "bin",
    ]
    for candidate in candidates:
        if (candidate / "clang").is_file() and (candidate / "ld").is_file():
            return candidate
    raise BuildError(
        "cannot find the JieLi PI32v2 toolchain; set FM1_PI32_TOOLCHAIN "
        "to the directory containing bin/clang and bin/ld")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 16), b""):
            digest.update(chunk)
    return digest.hexdigest()


def collect_sources(app_directory: Path) -> list[Path]:
    sources = sorted((ROOT / "platform").glob("*.c"))
    sources += sorted(app_directory.glob("*.c"))
    sources += sorted(app_directory.glob("*.S"))
    return sources


def object_name(source: Path) -> str:
    relative = source.relative_to(ROOT) if source.is_relative_to(ROOT) else source
    return re.sub(r"[^A-Za-z0-9._-]", "_", str(relative)) + ".o"


def identity_source(app_directory: Path, product: str) -> tuple[str, str]:
    entries: list[str] = []
    directories = [ROOT / "platform", ROOT / "include", app_directory]
    for directory in directories:
        if not directory.is_dir():
            continue
        for path in sorted(p for p in directory.rglob("*") if p.is_file()):
            relative = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
            entries.append(f"{relative}:{sha256(path)}")
    identity = hashlib.sha256("\n".join(entries).encode()).hexdigest()
    raw = hashlib.sha256("\n".join(entries).encode()).digest()

    byte_list = ", ".join(f"0x{byte:02x}" for byte in raw)
    text = (
        '#include "fm1_layout.h"\n'
        f"const struct fm1_firmware_layout fm1_layout = "
        f"{{{USER_BYTES}U, {UPDATE_FAMILY}U}};\n"
        f"const unsigned char fm1_build_id[32] = {{{byte_list}}};\n"
        f'const unsigned char fm1_update_identity[27] = "{product}";\n'
    )
    return identity, text


def check_entry(elf: Path) -> None:
    data = elf.read_bytes()
    if len(data) < 52 or data[:4] != b"\x7fELF":
        raise BuildError("linked output is not an ELF file")
    entry = struct.unpack_from("<I", data, 24)[0]
    if entry != ENTRY_POINT:
        raise BuildError(f"entry point is 0x{entry:08x}, expected 0x{ENTRY_POINT:08x}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=ROOT / "examples",
                        help="directory with application sources")
    parser.add_argument("--toolchain", default=None)
    parser.add_argument("--output", type=Path, default=BUILD)
    parser.add_argument("--product", default="FM-1_8000014",
                        help="update identity, FM-1_8 followed by six digits")
    parser.add_argument("--package", action="store_true",
                        help="also create a .fwsc update image")
    parser.add_argument("--clean", action="store_true")
    arguments = parser.parse_args()

    try:
        if not PRODUCT_RE.fullmatch(arguments.product):
            raise BuildError("invalid --product value")
        output = arguments.output.resolve()
        if arguments.clean and output.exists():
            shutil.rmtree(output)

        app = arguments.app.resolve()
        if not any(app.glob("*.c")):
            raise BuildError(f"no C sources in application directory {app}")

        bin_directory = find_toolchain(arguments.toolchain)
        clang = bin_directory / "clang"
        linker = bin_directory / "ld"
        objcopy = bin_directory / "objcopy"
        print(f"toolchain: {bin_directory}")

        objects_directory = output / "obj"
        generated = output / "generated"
        objects_directory.mkdir(parents=True, exist_ok=True)
        generated.mkdir(parents=True, exist_ok=True)

        includes = [
            ROOT / "include",
            ROOT / "sdk" / "include",
            ROOT / "sdk" / "include" / "libc",
            ROOT / "platform",
            app,
        ]
        include_flags = [f"-I{directory}" for directory in includes]

        sources = collect_sources(app)
        objects: list[Path] = []
        for source in sources:
            relative = source.relative_to(ROOT) if source.is_relative_to(ROOT) \
                else source.name
            flags = COMMON_FLAGS + include_flags
            flags += EXTRA_FLAGS.get(str(relative), [])
            target = objects_directory / object_name(source)
            print(f"compile {relative}")
            run([str(clang), *flags, "-MMD", "-MF", f"{target}.d",
                 "-c", str(source), "-o", str(target)])
            objects.append(target)

        identity, source_text = identity_source(app, arguments.product)
        identity_file = generated / "identity.c"
        identity_file.write_text(source_text)
        identity_object = objects_directory / "identity.o"
        print("compile generated/identity.c")
        run([str(clang), *COMMON_FLAGS, *include_flags, "-c",
             str(identity_file), "-o", str(identity_object)])
        objects.append(identity_object)

        elf = output / "sdk.elf"
        map_file = output / "sdk.map"
        link = [
            str(linker), "-o", str(elf), "--gc-sections",
            "-T", str(ROOT / "platform" / "link.ld"),
            *[str(path) for path in objects],
            "--start-group",
            "--whole-archive", str(ROOT / "sdk" / "lib" / "libfm1_engine.a"),
            "--no-whole-archive",
            str(ROOT / "sdk" / "lib" / "libc.a"),
            str(ROOT / "sdk" / "lib" / "libcompiler_rt.a"),
            "--end-group",
            "--sort-common",
            f"-M={map_file}",
            *[f"--undefined={name}" for name in LD_UNDEFINED],
        ]
        print(f"link {elf}")
        run(link)
        check_entry(elf)

        image = output / "app.bin"
        run([str(objcopy), "-O", "binary", "-j", ".text", str(elf), str(image)])
        print(f"identity: {identity}")
        print(f"firmware: {elf}")
        print(f"image:    {image} ({image.stat().st_size} bytes)")

        if arguments.package:
            package = output / "fm1.fwsc"
            script = ROOT / "tools" / "package.py"
            run([sys.executable, "-I", str(script), str(image),
                 "--output", str(package), "--product", arguments.product])
            print(f"package:  {package}")
    except (BuildError, OSError) as error:
        print(f"build: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
