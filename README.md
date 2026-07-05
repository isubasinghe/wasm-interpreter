# wasm-vm

An independent WebAssembly bytecode interpreter written in C++20.

This project began life as the starter code for [CMU's 17-770 compilers course (Project 1)](https://www.cs.cmu.edu/~btitzer/cs17-770/fall2024/proj1.html), but it has since grown well past the assignment into a standalone Wasm runtime and a personal learning platform for exploring interpreter and compiler internals. The current focus is on adding a **JIT compiler** alongside the existing tree-walking interpreter.

The core [WebAssembly specification](https://webassembly.github.io/spec/core/intro/index.html) is the reference of record for the implementation.

## Status

| Component | State |
|-----------|-------|
| Binary parser → `WasmModule` IR | working |
| Stack-based interpreter | working (incl. `br_table`, `call_indirect`) |
| Validator | in progress |
| JIT compiler | planned (using [xbyak](https://github.com/herumi/xbyak)) |

## Layout

| Path | Purpose |
|------|---------|
| `main.cpp` | Entry point: parse args, load the binary, run the interpreter |
| `inc/` | Public headers (`ir.h`, `parse.h`, `interpreter.h`, `validator.h`, `jit.h`, …) |
| `src/parse.cpp` | Decodes a Wasm binary into a `WasmModule` (static module state) |
| `src/interpreter.cpp` | The execution engine: dynamic state + the interpreter loop |
| `src/validator.cpp` | Bytecode validation (WIP) |
| `src/jit.cpp` | JIT backend (WIP) |
| `src/opcode_table.c` | The set of opcodes the VM recognizes |
| `inc/common.h` | Helpers, including LEB128 parsing |
| `tests/` | Sample programs and fixtures |
| `grade.sh` | Test harness / scoring script |

## Building

The interpreter is C++20 and depends on [`spdlog`](https://github.com/gabime/spdlog) and [`fmt`](https://github.com/fmtlib/fmt). [`wabt`](https://github.com/WebAssembly/wabt) (`wat2wasm` / `wasm2wat`) is invaluable for inspecting and authoring test cases, and `clang` is used to compile the C test samples.

### CMake / Make

```sh
make build        # configure + build into ./build, symlinks ./wasm-vm
make build-tests  # build the test programs
make run-tests    # build everything and run the grader
make clean
```

### Nix

A `flake.nix` provides a reproducible dev shell with the full toolchain (cmake, clang, ccls, wabt, wasmtime, xbyak, spdlog, and a Shake-enabled GHC):

```sh
nix develop       # drop into the dev shell
nix build         # build the wasm-vm package
```

### Shake (alternative)

A Haskell [Shake](https://shakebuild.com/) build is also available:

```sh
runghc Shakefile.hs        # build
runghc Shakefile.hs tests  # build tests
runghc Shakefile.hs test   # run the grader
```

## Running

```sh
wasm-vm [--trace] [--file-log] [--jit-binary <output-file>] [-a <space-separated args>] <input.wasm>
```

- `--trace` — emit an execution trace to `stderr`
- `--file-log` — route logging to a file
- `--jit-binary <output-file>` — compile the exported `main` with the JIT and write the generated machine-code bytes
- `-a <args...>` — arguments passed to the program's `main`
- The entry point is the exported function named `main`.

## Conventions

- `f64` outputs are printed with 6 digits after the decimal point.
- Program output (including `!trap`) goes to `stdout`; `stderr` is kept clean.

## Acknowledgements

The original parser scaffolding and project framing came from CMU's 17-770 course materials. Everything beyond that — the interpreter, the validator, and the forthcoming JIT — is independent work.
