<h1 align="center">
  Modernized Latex
</h1>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/compiler_support">
    <img alt="C++ Standard" src="https://img.shields.io/badge/C%2B%2B-20-blue?logo=c%2B%2B"
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

A complete rewrite of the LaTeX typesetting system, celebrated for its professionalism and engineered in C++ for maximum compatibility and speed.

LaTeX has long been the gold standard for typesetting documents. However, while standard LaTeX provides a powerful high-level descriptive markup language, it is often perceived as slow and unforgiving. This is largely due to its decades-old macro-processing mechanism, which inherently limits execution speed and consumes excessive overhead.

mLaTeX (also known as LaTeX+) was not built to be just a simple clone. It is a ground-up rewrite that keeps the familiar syntax and language intact while providing a minimalist footprint and unparalleled execution speed—even when compared to modern alternatives like [Typst](https://github.com/typst/typst). I engineered it for maximum capacity and security, supporting robust document generation while maintaining the comfortable stability of traditional LaTeX. It continues to surpass standard word processors like [Microsoft Word](https://en.wikipedia.org/wiki/Microsoft_Word) in mathematical typesetting fidelity. The fundamental user experience remains unchanged; rather, the underlying architecture has evolved so that veteran authors can transition seamlessly.

## Installation

The mLaTeX CLI is available from various sources:

* You can get the source code and pre-built binaries for the latest release of mLaTeX from the releases page. Download the archive for your platform and place it in a directory that is in your PATH. To stay up to date with future releases, simply run `mlatex update`.
* You can also install mLaTeX through different package managers. Note that the versions in package managers might lag slightly behind the latest release.
* Linux:
* macOS:
* Windows:

> Note: No packages have been released at this time.

## Usage

Once you have mLaTeX correctly set up, you can run it via the command line:

```sh
# Get help information from the CLI
mlatex --help

# Check the installed version
mlatex --version

```

You can also compile your PDF directly by providing an input and output file:

```sh
mlatex compile --input input.mtex --output output.pdf # --jit

```

> By enabling the `--jit` parameter, you instruct the engine to compile Just-In-Time.

## Community & Support

The primary hub for the project is the Discord server. It is a great place to ask questions, discuss contributing, or just chat. I would be happy to see you there!

* **Join the Discord:** [https://discord.gg/null](https://discord.gg/null)

As the sole maintainer of this project, I want to ensure everyone has a positive experience. If you have questions, need to discuss the project's direction, or have any concerns, please feel free to reach out to me directly:

* **Contact:** Andres Hernandez at andromedeyz@hotmail.com

## Contributing

I highly encourage and appreciate contributions from the community. If you experience bugs, feel free to open an issue. If you would like to implement a new feature or a bug fix, please follow the steps outlined in the [contribution guide](https://github.com/aveloux/CONTRIBUTING.md).

To build mLaTeX yourself, first ensure that you have a C++23 compatible compiler and [CMake](https://cmake.org/) installed. Then, clone this repository and follow the directions available on the [official mLaTeX documentation page](https://github.com/aveloux/docs).

## Acknowledgements

To myself, Andres Hernandez [@ApaxPhoenix](https://github.com/ApaxPhoenix), Christ is King.
