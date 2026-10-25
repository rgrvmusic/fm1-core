# FM-1 Core platform

A small MIT-licensed hardware platform for the M-VAVE FM-1 synthesizer
(JieLi AC791N / WL82, PI32v2). It is based on the FM1-open firmware, with
the GPL-3.0 application and web code removed and the board layer rewritten.
This tree contains no GPL-3.0 code.

It provides the display, key matrix, encoders, LEDs, ADC controls, ALNK
audio DMA, and a build and packaging path. The FM engine is the binary
`sdk/lib/libfm1_engine.a` supplied by M-VAVE.


## Status

The platform and example application build, link and package into a `.fwsc`
update image. Nothing has run on hardware here yet; keep a stock recovery
image and know how to enter the bootloader before flashing.


## Layout

- `include/`: platform API and application hooks.
- `platform/`: timebase, panel, display, audio, mixer, weak hooks, linker script.
- `examples/sine.c`: example application.
- `tools/build.py`: build driver.
- `tools/package.py`: `.fwsc` writer.
- `sdk/`: binary engine, libc/compiler-rt, boot resources.


## Building

You need the JieLi PI32v2 toolchain 2.5.3 (clang 4.0.1, `-mcpu=r3`) and
Python 3.10 or newer. If the toolchain is not under `~/.jieli/toolchain`,
set `FM1_PI32_TOOLCHAIN`.

```sh
python3 tools/build.py --clean    # build/sdk.elf, build/app.bin
python3 tools/build.py --package  # also build/fm1.fwsc
```

Use `--app path/to/app` to build another application directory. The build
compiles every `.c` and `.S` file directly inside it.


## Hooks

The engine calls `groove_*` functions. The platform supplies weak defaults,
so a checkout links without an application. An application can also replace
`fm1_app_render`, `fm1_output_mix` and the `fm1_audio_*` functions. See
`include/fm1_app.h`.


## License

`platform/`, `include/`, `tools/` and `examples/` are MIT licensed; see
[LICENSE](LICENSE). The binary SDK, boot resources and third-party
components keep their own terms, recorded in [THIRD_PARTY.md](THIRD_PARTY.md).

FM-1 and M-VAVE are trademarks of their owner. This is an independent
project.
