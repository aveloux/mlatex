# @aveloux/mlatex

A LaTeX typesetting engine, rewritten from the ground up in modern C++ and compiled to WebAssembly. It turns LaTeX into PDF in a web page, a worker or Node, with no TeX installation behind it and nothing sent anywhere.

```bash
npm install @aveloux/mlatex
```

## A document

```js
import Latex from "@aveloux/mlatex";

const engine = await Latex();
const session = new engine.Session();

const made = session.typeset(String.raw`\documentclass{article}
\begin{document}
Hello, \LaTeX.
\end{document}`);

if (!made) console.error(session.error());
const pdf = session.pdf(); // a Uint8Array of its own
session.delete();
```

CommonJS works the same way: `const Latex = require("@aveloux/mlatex")`.

## A session

A session keeps what it is handed from one document to the next, so a program that makes a thousand invoices sets up its letterhead once.

| Operation | What it does |
| --- | --- |
| `set(name, value)`, `unset(name)` | A value the document reads with `\variable{name}`. |
| `define(name, arity, handler)`, `forget(name)` | A command your code answers: `handler` is called with each argument's text, and what it returns is set in the command's place. |
| `provide(name, content)`, `withdraw(name)` | A file under the name a document uses for it, as a `Uint8Array` or a string: `letterhead/main.mtex` for `\usepackage{letterhead}`, `chapter.tex` for `\input{chapter}`, `logo.png` for `\includegraphics{logo.png}`, `references.bib` for `\bibliography{references}`. |
| `typeset(document)` | Sets a document, and says whether it was set with nothing reported. |
| `snippet(text)` | Sets a piece of LaTeX alone, such as a formula or a table, on a page cropped to it. |
| `pdf()` | The last PDF made. |
| `error()` | What went wrong, one problem per line: `appendix.tex:3:5: error (macro): Undefined control sequence \foo`. |
| `delete()` | Frees the session, as every object Emscripten binds must be. |

```js
session.set("customer", "Acme Corporation");
session.define("balance", 1, (account) => ledger[account].toFixed(2));
session.provide("letterhead/main.mtex", String.raw`\newcommand{\letterhead}{\textbf{Acme}}`);
session.provide("logo.png", new Uint8Array(await logo.arrayBuffer()));
```

A package is a folder holding one file, `main.mtex`, written in the same language as a document; `.sty` and `.cls` files are not read. Classes are part of the engine.

## Where it runs

The package holds three files: `latex.js`, which loads the engine, `latex.wasm`, the engine itself, and `latex.data`, the fonts, packed into the engine's own file system.

- **Node** takes the package's own entry, which finds `latex.wasm` and `latex.data` beside it.
- **A worker or a page** loads `latex.js` with the other two files beside it, served from the same folder: `importScripts("/engine/latex.js")` in a classic worker, then `Latex()`.
- **A bundler** that moves `latex.js` away from the other two says where they are:

```js
const engine = await Latex({ locateFile: (name) => `/engine/${name}` });
```

The engine starts no threads and makes no network requests: a picture from a URL is fetched by your code and handed in with `provide`.

## Links

- [Source and issues](https://github.com/aveloux/mlatex)
- [Releases](https://github.com/aveloux/mlatex/releases), with the command line for Windows, macOS and Linux

mLaTeX is licensed under the GNU Affero General Public License, version 3.
