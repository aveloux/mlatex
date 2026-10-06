# jit

Reserved for the just-in-time target, `latex --target=jit`. It is empty on
purpose: this part is designed and written by hand, and nothing is put here
until it is.

## Where it plugs in

- **The command line.** `src/main.cpp` accepts `--target=jit` (`-t jit`). For
  now a run with it says the target is not built yet and exits with status 0
  without typesetting anything. That branch, `if (target != "aot")`, is where
  the just-in-time path gets called.
- **The ahead-of-time path it sits beside.** `latex::compose` in
  `src/latex.cpp` takes a document through the lexer, the mouth (expansion),
  the parser, the composer and the typesetter, then writes the PDF.
  `--time-statistics` prints every one of those stages and how long it took,
  and `-T`/`--time` prints the run as a whole. Run both targets on the same
  document to see what the JIT saves.

## How it builds

- Every `.cpp` under `src/` is compiled into the `core` library
  (`file(GLOB_RECURSE sources ...)` in `CMakeLists.txt`), and every folder
  under `src/` and `include/` is on the include path. A file added here
  builds without touching CMake; configure again after adding one.
- A public header goes under `include/jit/`.
- The C library (`include/network/ffi.hpp`) and the WebAssembly module
  (`include/render/wasm.hpp`) are separate targets, and they get the JIT only
  if they are changed to use it.

## House rules

- Every name is one real word: no underscores, no camelCase. Functions take
  their verbs from `get`, `set`, `post`, `compose` and `dispose`.
- Each `src/jit/<name>.cpp` gets a standalone test at
  `tests/jit/test_<name>.cpp` that checks with plain `assert`.
- Every declaration has a Doxygen block, and the `docs` target builds with no
  warnings.
- Add an entry under `CHANGELOG.md`.
