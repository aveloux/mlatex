"""mLaTeX's C library, latex_ffi, with the fonts it reads beside it.

``library`` is the library's path, for ``ctypes.CDLL``.
"""

import sys
from pathlib import Path

library = str(
    Path(Path(__file__).parent, {"win32": "latex_ffi.dll", "darwin": "liblatex_ffi.dylib"}.get(sys.platform, "liblatex_ffi.so"))
)
