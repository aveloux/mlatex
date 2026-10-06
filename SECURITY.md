# Security Policy

mLaTeX compiles documents that other people write, often on servers and
inside other programs. This page explains which versions receive security
fixes, how to report a vulnerability, and what the engine does and does not
protect against.

## Supported versions

mLaTeX is pre-release software. Security fixes go to the `main` branch and
ship in the next release.

| Version | Supported |
| --- | --- |
| 0.1.x (`main`) | :white_check_mark: |
| Earlier snapshots | :x: |

## Reporting a vulnerability

**Please do not report security issues in public issues, pull requests or
discussions.**

Report them privately in one of two ways:

- through GitHub's private vulnerability reporting, using **Report a
  vulnerability** on the repository's **Security** tab; or
- by email to andromedeyz@hotmail.com, with `mLaTeX security` in the
  subject line.

Please include:

- the version and build (`latex --version`) and your operating system;
- the smallest document, command line or program that reproduces the issue;
- what an attacker gains: code execution, reading or writing files,
  network access, denial of service or something else.

### What to expect

| Step | Target |
| --- | --- |
| Acknowledgement of your report | within 3 business days |
| Initial assessment and severity | within 10 business days |
| Fix for a confirmed critical or high-severity issue | as soon as possible, usually within 30 days |

You will be kept informed while the issue is being fixed. Please allow up to
90 days for a fix before disclosing the issue publicly. Unless you ask to
stay anonymous, the fix's entry in [CHANGELOG.md](CHANGELOG.md) will credit
you.

## Security model

mLaTeX treats every document as untrusted input. Unlike a TeX distribution,
it has no way of running code that the document supplies outside the
engine's own language.

### What the engine guarantees

- **No shell escape.** `\write18` and every other way of running a program
  does not exist. A write to stream 18 is logged and nothing more.
- **No files written by the document.** `\openout` and `\write` to a file
  stream do nothing, and `filecontents` keeps its file in memory for that one
  run. The only file the engine writes is the PDF, at the path the command
  line or the host program chose.
- **No TeX code loaded from disk.** `.sty` and `.cls` files are never read.
  Packages are compiled into the engine, handed in by the host program, or
  written as `name/main.mtex` in the engine's own language.
- **Confined file reads.** Inputs, packages, pictures and bibliographies are
  read only from the document's own folder and from folders given with `-I`
  (`latex::Host::directories` for a program). Absolute paths and paths that
  climb out with `..` are refused. A document handed in from memory can read
  only the host's folders and the files the host provided.
- **No auxiliary files.** Cross-references, the table of contents and the
  bibliography are resolved in memory, so nothing is left behind for a later
  run to trust.

### What you need to handle yourself

- **Network access.** A document can make HTTP and HTTPS requests from the
  machine that compiles it: `\includegraphics{https://...}`, `\httpget{url}`
  and `\httppost{url}{body}`. These use WinHTTP on Windows and libcurl on
  Linux and macOS when the build found it. The WebAssembly module makes no
  requests. When compiling documents you do not trust, run the engine
  without network access, for example in a container or behind a firewall
  rule, so that a document cannot reach internal services or send data out.
- **Time and memory.** As with TeX, a document can loop forever or use a
  large amount of memory. The engine does not limit either. A service that
  compiles untrusted documents should enforce its own timeout and memory
  limit (a job object on Windows, cgroups or `ulimit` elsewhere).
- **Commands a host program implements.** A command defined with
  `latex::Session::define`, or `define` in the C library or the WebAssembly
  module, runs with the host program's privileges, and the document chooses
  its arguments. Validate them as you would any other untrusted input.
- **Third-party libraries.** Fonts and pictures are parsed by HarfBuzz,
  FreeType, libpng, libjpeg-turbo, libwebp and zlib. Keep them up to date
  (through your vcpkg baseline, or the system's packages on Linux). Report a
  flaw in one of them to its own project as well; how mLaTeX uses them is in
  scope here.

## Scope

In scope: the engine (`core`), the `latex` command line, the C library
(`latex_ffi`), the WebAssembly module, the packages under `src/modules`, and
the build and packaging scripts in this repository.

Out of scope: denial of service caused only by a document's size or
complexity, findings that depend on a modified build, and documents that ask
for network access through the commands listed above.
