# Pesto/11

Cross-compiler from the **Turbo Pascal 3.0** dialect to **PDP-11** assembly (MACRO-11), for Soviet PDP-11 compatible machines.

- First target: UKNC (Электроника МС 0511) under RT-11. The output `.MAC` is assembled with `macro11` and linked with `pclink11` into a `.SAV`.
- Planned target: BK-0010 with the BKTurbo8 assembler.

**Status:** early development, the compiler does not compile programs yet.

## Building

Linux / macOS:

```sh
make
make test
```

Windows: open `pesto11-VS2022.sln` in Visual Studio 2022, build, then run `!tests-run.cmd` from the repository root.

Requires a C++17 compiler, no other dependencies.

## Usage

```
pesto11 [options] PROG.PAS
```

Run `pesto11 --help` for the list of options.

## Documentation

Design documents are in [`docs/`](docs/) (in Russian):

- [`TP3-dialect.md`](docs/TP3-dialect.md) — the language accepted by Pesto/11 and its deviations from Turbo Pascal 3.0.
- [`DESIGN.md`](docs/DESIGN.md) — compiler architecture and code generation.

## License

MIT, see [LICENSE](LICENSE).
