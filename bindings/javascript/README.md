# mlatex

mLaTeX compiled to WebAssembly: it typesets LaTeX into PDF in web pages, workers and Node, with no TeX installation behind it.

```bash
npm install mlatex
```

```js
import Latex from "mlatex";

const engine = await Latex();
const session = new engine.Session();

session.set("customer", "Acme Corporation");
if (!session.typeset(source)) console.error(session.error());
const pdf = session.pdf();
session.delete();
```

The package holds `latex.js`, which loads the engine, `latex.wasm`, and `latex.data`, the fonts. A bundler that moves `latex.js` away from the other two needs `locateFile` to say where they are.

See [Embedding](https://github.com/aveloux/mlatex#readme) for the operations. mLaTeX is licensed under the GNU Affero General Public License, version 3.
