<h1 align="center">
  mLaTeX
</h1>

<p align="center">
  <b>A LaTeX typesetting engine, rewritten from the ground up in modern C++.</b>
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/compiler_support">
    <img alt="C++ Standard" src="https://img.shields.io/badge/C%2B%2B-26-blue?logo=c%2B%2B"
  ></a>
  <a href="https://github.com/aveloux/mlatex/actions">
    <img alt="Build Status" src="https://img.shields.io/github/actions/workflow/status/aveloux/mlatex/build.yml?branch=main&label=build"
  ></a>
  <a href="https://github.com/aveloux/mlatex">
    <img alt="Platforms" src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey"
  ></a>
  <a href="https://github.com/aveloux/mlatex/stargazers">
    <img alt="GitHub Stars" src="https://img.shields.io/github/stars/aveloux/mlatex?color=239dad&label=stars"
  ></a>
  <a href="https://github.com/aveloux/mlatex/issues">
    <img alt="GitHub Issues" src="https://img.shields.io/github/issues/aveloux/mlatex?color=5865F2&label=issues"
  ></a>
  <a href="https://github.com/aveloux/mlatex/pulls">
    <img alt="PRs Welcome" src="https://img.shields.io/badge/PRs-welcome-A561FF"
  ></a>
  <a href="https://github.com/aveloux/mlatex/commits/main">
    <img alt="Last Commit" src="https://img.shields.io/github/last-commit/aveloux/mlatex?color=success"
  ></a>
  <a href="https://github.com/aveloux/mlatex/blob/main/LICENSE">
    <img alt="AGPL v3 License" src="https://img.shields.io/badge/license-AGPL%20v3-brightgreen"
  ></a>
</p>

mLaTeX (Modernized LaTeX) reads LaTeX as people already write it and turns it
into PDF with a single program. There is no TeX distribution behind it, no
format files, no package tree on disk and no auxiliary files between runs.
The lexer, macro expander, parser, line breaker, page builder, font shaper and
PDF writer are all part of one engine, written in C++26.

The language stays the same. A document written for pdfLaTeX, its preamble
included, is meant to compile unchanged. What changes is the machinery
underneath: the work LaTeX does with thousands of lines of TeX macros is done
once, in compiled code.

## Highlights

- **Reads existing LaTeX.** 509 packages and 14 document classes, each
  checked by a sample document of its own in `build/packages/`, plus a
  glossary of about 630 more commands from CTAN packages that have no folder
  of their own. An unknown package produces a warning, not a failed build.
- **Fast.** On a release build, a one-page article takes a few milliseconds,
  and the 11-page sample paper in `build/main.mtex`, two passes included,
  takes under 30 ms.
- **Self-contained.** Packages and English hyphenation are compiled into the
  binary. Fonts (New Computer Modern, STIX Two, Fira Math, TeX Gyre and
  others) and hyphenation patterns for 79 languages and variants ship in
  `assets/`.
- **The parts people use.** Mathematics; tables; floats; cross-references
  resolved by a second pass in memory; tables of contents and indexes;
  hyperref links and bookmarks; TikZ, pgfplots, tikz-cd, xy-pic and forest
  pictures; beamer slides with overlays and themes; BibTeX bibliographies in
  the standard, natbib and biblatex styles; right-to-left and CJK text;
  PNG, JPEG, WebP, EPS and SVG pictures; PDF/A.
- **Embeddable.** The same engine is available as a C++ library, a C library
  that any language with a foreign-function interface can bind, and a
  WebAssembly module. Programs can supply values, commands they implement,
  and files from memory, then get the PDF back as bytes.
- **Safe by design.** A document cannot run shell commands (`\write18` does
  not exist), cannot write files, and never loads `.sty` or `.cls` code from
  disk. With `--offline` it cannot reach the network either. See
  [SECURITY.md](SECURITY.md).

## Status

mLaTeX is pre-release software (version 0.1.0). The `aot` target, which
writes the PDF directly, is complete. The `jit` and `wasm` targets of the
command line are accepted but not yet implemented; the just-in-time target
is reserved in [`src/jit`](src/jit/implementation.md). See
[CHANGELOG.md](CHANGELOG.md) for what has been done and what is
intentionally left out.

## Installation

Prebuilt packages have not been published yet. When they are, each release
will include:

| Platform | Package | Installs to |
| --- | --- | --- |
| Windows | `.msi` installer, `.zip` | `C:\Program Files\mLaTeX` |
| macOS | `.dmg` disk image, `.tar.gz` | wherever the `mLaTeX` folder is dragged |
| Linux | `.deb`, `.rpm`, `.tar.gz` | `/opt/mlatex` |

Each package is named for its platform and architecture, for example
`mlatex-0.1.0-windows-x64.msi` or `mlatex_0.1.0_arm64.deb`. The program is
`bin/latex` inside the installed folder. Add that `bin` folder to your `PATH`
to run it from anywhere. On Linux it is kept out of `/usr/bin` so that it
does not conflict with TeX Live's own `latex`.

## Building from source

### Requirements

- CMake 3.25 or later and Ninja.
- A C++26 compiler with `#embed`: Clang 19 or later, or GCC 15 or later. On
  Windows, use the `clang-cl` that Visual Studio installs under
  `VC/Tools/Llvm`. The build selects it automatically if your IDE profile
  names MSVC (`cl`) or MinGW.
- [vcpkg](https://vcpkg.io) with HarfBuzz, libpng, libjpeg-turbo, libwebp
  and zlib installed for your target triplet.
- [gperf](https://www.gnu.org/software/gperf/).
- Optional: libcurl on Linux and macOS for `\includegraphics` of `https://`
  sources (Windows uses WinHTTP); Doxygen for the API reference; WiX
  (version 3, or the `wix` .NET tool, version 4 or later) for an `.msi`.

```bash
vcpkg install harfbuzz libpng libjpeg-turbo libwebp zlib
```

### Build and test

```bash
cmake -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
```

```bash
cmake --build cmake-build-release
```

```bash
ctest --test-dir cmake-build-release --output-on-failure
```

On Windows, the `debug`, `release` and `wasm` presets in
`CMakePresets.json` set all of this up (`cmake --preset release`). If you
leave out the toolchain file, the build looks for vcpkg in `VCPKG_ROOT` and
then in `C:/vcpkg`.

The build produces three targets: `latex` (the command-line program), `core`
(the engine as a static library, which every other target links) and `ffi`
(the C library, `latex_ffi`). Every source file has a matching test under
`tests/`.

### Packaging

From a Release build tree:

```bash
cpack --config cmake-build-release/CPackConfig.cmake -B packages
```

This produces the packages listed under [Installation](#installation) for
the platform and architecture of the build. To package another
architecture, run a separate build for it, for example `clang-cl` targeting
ARM64 with the `arm64-windows` triplet. On macOS,
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` with universal libraries
produces one disk image for both.

## Usage

```bash
latex paper
```

This compiles `paper.mtex` or, failing that, `paper.tex`, and writes
`paper.pdf` beside it. Options accept one or two dashes and are written as
words separated by dashes. TeX's own spellings are accepted too, so scripts
written for pdfLaTeX keep working.

```bash
latex --interaction=batch-mode --halt-on-error paper
```

```bash
latex --output-directory=out --job-name=final paper.tex
```

```bash
latex -I styles -I figures paper
```

```bash
latex --watch --open paper
```

```bash
latex --time paper
```

| Option | Meaning |
| --- | --- |
| `-o`, `--output-directory=DIR` | Write the PDF into `DIR` |
| `-j`, `--job-name=NAME` | Name the PDF `NAME.pdf` |
| `-n`, `--draft-mode` | Check the document and write no PDF |
| `--halt-on-error` | Write no PDF if the document has an error |
| `-I`, `--include-directory=DIR` | Also look in `DIR` for inputs, packages, pictures and bibliographies |
| `--offline` | Make no network requests; for documents you did not write |
| `-w`, `--watch` | Recompile every time the document is saved |
| `-O`, `--open` | Open the PDF in the system viewer when it is written |
| `-i`, `--interaction=MODE` | `batch-mode` prints only errors |
| `--file-line-error` | Print errors as `file:line:column: message` |
| `-T`, `--time` | Print how long the run took |
| `--time-statistics` | Print every stage of the engine and its timing |

`latex --help` lists every option. The exit status is 0 for a clean
document, 1 for a document with errors (a PDF is still written unless
`--halt-on-error` is given), and 2 for a mistake on the command line.

### Bibliographies

`\bibliography{refs}` and biblatex's `\addbibresource{refs.bib}` read a
`.bib` file directly, so there is no separate BibTeX step. A `.bib` file can
also be imported with `\input{refs.bib}`. Running `latex refs.bib`
typesets every entry in the file, which is useful for checking a
bibliography before citing it.

### Writing packages

A package is a folder containing `main.mtex`, written in the same language
as a document. See [src/modules/packages.md](src/modules/packages.md).

## Embedding the engine

The command line is a thin layer over `latex::Session`, and every binding
exposes that same interface under the same names.

**C++**

```cpp
latex::Session session(latex::locate(program));
session.set("customer", "Acme Ltd.");
session.define({.name = "balance", .arity = 1, .handler = [&](auto arguments) {
    return ledger.balance(arguments[0]);
}});
session.provide("chart.png", png);

if (!session.typeset(document)) std::cerr << session.error;
send(session.pdf);
```

**C**, or any language that can call C (Python's ctypes, C#'s P/Invoke,
Rust, Go). The library exports a single function, `engine()`, declared in
`include/network/ffi.hpp`:

```c
const Latex* table = engine();
Session* session = table->compose(NULL);
table->set(session, "customer", "Acme Ltd.");

const unsigned char* pdf;
size_t size;
if (!table->typeset(session, source, strlen(source), &pdf, &size)) {
    fprintf(stderr, "%s", table->error(session));
}
table->dispose(session);
```

**JavaScript**, through the WebAssembly module built with the `wasm` preset
(see `include/render/wasm.hpp`):

```js
const engine = await Latex();
const session = new engine.Session();
session.set("customer", "Acme Ltd.");
if (!session.typeset(source)) console.error(session.error());
const pdf = session.pdf();
```

## Documentation

The API reference is generated from the sources with Doxygen:

```bash
cmake --build cmake-build-release --target docs
```

It is written to `cmake-build-release/docs/html`. The reference and
[src/modules/packages.md](src/modules/packages.md) cover the engine's internals and
package authoring. [CHANGELOG.md](CHANGELOG.md) describes every feature in
detail.

## Project layout

| Path | Contents |
| --- | --- |
| `include/`, `src/` | The engine: `syntax` (lexer, expander, parser), `render` (layout, typography, graphics, PDF), `memory` (arenas, tables), `network` (HTTP, the C library) |
| `src/modules/` | Packages and classes, one folder each, compiled into the binary |
| `src/jit/` | Reserved for the just-in-time target |
| `assets/` | Fonts, hyphenation patterns and the command glossary |
| `build/` | The sample paper and one sample document per package |
| `tests/` | One test per source file, at the same relative path |

## Contributing

Contributions are welcome. Please open an issue to report a bug or to
discuss a feature before starting on a large change. Pull requests should:

- build without warnings and pass `ctest`;
- add a test in the file that mirrors the source file they change;
- follow the existing style: every identifier is one real word, with no
  underscores and no camelCase, and every declaration has a Doxygen comment;
- add an entry to [CHANGELOG.md](CHANGELOG.md).

Everyone taking part is expected to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).

## Support

The project's Discord server is the place to ask questions, discuss
contributions and follow its direction:
[discord.gg/null](https://discord.gg/null).

mLaTeX is maintained by Andres Hernandez
([\@ApaxPhoenix](https://github.com/ApaxPhoenix)). For questions about the
project or its direction, write to andromedeyz@hotmail.com. To report a
security issue, follow [SECURITY.md](SECURITY.md) instead of opening a
public issue.

## License

mLaTeX is free software, licensed under the
[GNU Affero General Public License v3.0](LICENSE).

## Acknowledgements

mLaTeX builds on [HarfBuzz](https://harfbuzz.github.io) for text shaping
and font subsetting, [FreeType](https://freetype.org),
[libpng](http://www.libpng.org), [libjpeg-turbo](https://libjpeg-turbo.org),
[libwebp](https://developers.google.com/speed/webp) and
[zlib](https://zlib.net). Its fonts include New Computer Modern, STIX Two,
Fira Math, Asana Math and the TeX Gyre math fonts, and its hyphenation
patterns come from the [hyph-utf8](https://www.hyphenation.org) project.
Above all, it owes its language to Donald Knuth's TeX and Leslie Lamport's
LaTeX.

To myself, Andres Hernandez [\@ApaxPhoenix](https://github.com/ApaxPhoenix), Christ is King.
