# mlatex

mLaTeX's C library, `latex_ffi`, and the fonts it reads: it typesets LaTeX into PDF from Python, with no TeX installation behind it.

```bash
pip install mlatex
```

```python
import ctypes

import mlatex

library = ctypes.CDLL(mlatex.library)
```

`library.engine()` returns the struct of operations: `compose` a session, `set` values, `define` commands, `provide` files, `typeset` a document into PDF bytes, and `dispose` it. The fonts are found beside the library by themselves. See [Embedding](https://github.com/modern-latex/mlatex#readme) for the operations and the struct's fields.

mLaTeX is licensed under the GNU Affero General Public License, version 3.
