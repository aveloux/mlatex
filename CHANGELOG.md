# Changelog

Everything the engine gained since `e2e4b2c`, the commit that rebuilt the
expander and its primitive layer. Grouped by what a document or a program
sees, newest work last within each group.

## Unreleased — 2026-09-29

### Reading LaTeX that was written for LaTeX

- **The glossary.** `assets/glossary.mtex` holds about 630 commands from the
  CTAN packages the engine has no folder for — microtype, mathtools' and
  amsmath's remaining commands, cleveref's formats,
  etoolbox's command tools, xpatch, silence, kvoptions, fontspec without its
  package, wasysym, marvosym, bbding, textgreek, zref, varioref, fancyref,
  refcount, totcount, nth, numprint, fixme, changes, relsize, stackengine,
  isomath, derivative, esdiff, commath, mleftright, hepunits, eso-pic,
  draftwatermark, pdfcomment, animate, KOMA-Script's running heads, sectsty,
  titletoc, minitoc, etoc, floatrow, endnotes, environ and more. Each line is
  one command as `\@define` takes it, compiled in with `#embed` and indexed by
  the compiler (`syntax::Glossary`, `memory::Catalog`). Nothing is read until
  a document uses a name nothing else defines; the line is then defined for
  the rest of the run, `\long` as LaTeX's `\newcommand` is, and the name read
  again. A document that uses none of it pays nothing.
- **Unknown packages are warnings.** `\usepackage{anything}` never stops a
  document: an unknown package is marked loaded, named in a warning, and its
  commands found in the glossary when they are used.
- **About 360 of the 394 most-used CTAN packages load cleanly** (was 121):
  font, driver and internal packages are marked provided, grouped by why, in
  `core/compatibility.mtex`; a package that is another under a newer or older
  name is an alias there (`xurl`→`url`, `soulutf8`→`soul`, `subfigure`→`subfig`,
  `circuitikz`→`tikz`, `fvextra`→`fancyvrb`, `pgfplotstable`→`pgfplots`, …).
- **New packages**: `fontspec` and `unicode-math` (a named face set in the
  nearest one the engine carries), `pifont` (`\ding` as Unicode characters),
  `tikz-cd` (a diagram's objects in their grid), `pgfplots` (see Pictures),
  `fontawesome5` and `fontawesome` (icons as the nearest Unicode symbols, a
  dot for a brand's logo), `tocloft` (its lengths as real registers), `ctable`; and
  earlier in this cycle some sixty more — amsmath, amsthm, mathtools, physics,
  siunitx, hyperref, cleveref, natbib, biblatex, booktabs, tabularx, longtable,
  makecell, multirow, colortbl, caption, subcaption, subfig, float, listings,
  minted, fancyvrb, algorithm, algpseudocode, algorithm2e, enumitem, geometry,
  fancyhdr, setspace, titlesec, tikz, tcolorbox, mdframed, glossaries, acronym,
  todonotes, csquotes, soul, ulem, gensymb, eurosym, datetime2, etoolbox,
  ifthen, xfp and the rest listed in `src/modules/main.mtex`.
- **Document classes.** Any class is set as article is, at the size, sheet and
  column count its options ask for; an unknown one is a warning, not an error.
  `IEEEtran`, `acmart`, `llncs`, `elsarticle`, `revtex4-2` (and `revtex4-1`,
  `revtex4`), `amsart`, `svjour3` and KOMA-Script's `scrartcl`, `scrreprt` and
  `scrbook` have modules of their own: margins, columns, Roman section numbers
  for IEEE, and title blocks that set affiliations, institutes, addresses and
  keywords. IEEEtran's abstract and index terms run in (`Abstract---`), its
  captions are `Fig. 1.` and `TABLE I`, its authors stand in columns side by
  side, and its references are in footnote size; llncs declares its theorems
  and its `credits`; acmart's `CCSXML` is read and let go, its `teaserfigure`
  is a figure and its `acks` a heading of their own; revtex's
  `acknowledgments` and `ruledtabular` are read.
- **beamer.** Each frame a page of its own, 128 by 96 mm, in eleven-point sans
  serif, ragged right: its title in the structure's blue at its head -- given
  as its argument or by the `\frametitle` it opens with -- and what it holds
  in the middle of the rest, at the top for `[t]`. `\frame{\titlepage}`, the
  title page's title, subtitle, author, institute and date; `block`,
  `alertblock` and `exampleblock` under a coloured bar; `columns`, as blocks
  and as `\column`; blue triangles for bullets; an outline for
  `\tableofcontents`, and `\AtBeginSection`'s frames. Overlays -- `\pause`,
  `\only<2>`, `\item<2->`, `\alert<2>`, `\uncover` -- are the frame's last
  slide, as a handout sets it; themes, colour themes and templates are read.
- **The letter class**: `\address`, `\signature`, the `letter` block, and
  `\opening`, `\closing`, `\ps`, `\encl` and `\cc` laid out as letter.cls lays
  them out.
- A book's `\frontmatter`, `\mainmatter` and `\backmatter`: roman pages and
  unnumbered chapters that are still listed in the contents, then arabic from
  one. LaTeX's `titlepage`: a page of its own, unnumbered.
- **`\newenvironment` is native.** A document's own `\newenvironment`,
  `\renewenvironment`, `\provideenvironment` and xparse's
  `\NewDocumentEnvironment` and its kin make a block whose begin and end code
  run inside the block's group, as LaTeX's do -- the end code may close what
  the begin code opened. One of a name the engine sets itself is the
  document's; one it redefines keeps the engine's, with a warning.
- **`\begin{name}` as LaTeX reads it**: a block of no native hooks runs
  `\name` and `\endname` when they exist -- `\begin{small}`, a pair a
  document defined with `\def` -- and one meaning nothing is a warning,
  `Environment name undefined`, and set as a group.
- The kernel's internals a preamble borrows: `\z@`, `\p@`, `\@ne`…`\@MM`,
  `\m@ne`, `\@plus`, `\@minus`, the `\@temp` registers, `\count@`, `\dimen@`,
  `\skip@`, `\toks@`, `\@nil`, `\@nnil`, `\@for` and `\@tfor`,
  `\@startsection` (a class's own headings, run-in ones included),
  `\@addtoreset`, `\numberwithin`, `\counterwithin*`, and a macro whose
  parameter text starts with a delimiter (`\def\a<#1>{...}`).
- `\DocumentMetadata{...}` before `\documentclass` is read; its
  `pdfstandard` makes the file PDF/A (see The file).
- **`filecontents`**, LaTeX's kernel block: the file a document carries —
  before `\documentclass` or anywhere after — is kept for the run, so the
  `\bibliography` or `\input` naming it later reads it. Nothing reaches disk.
- **A `.bbl` beside the document** is read when none of the `.bib` files
  `\bibliography` names can be, as a paper sent to a journal or the arXiv
  ships; natbib-style `\bibitem[Knuth(1984)]{knuth}` entries cite by author
  and year.
- **`\jobname`** is the document's file name without its extension.
- **`\ifpdf`, `\ifPDFTeX`, `\ifxetex`, `\ifluatex`** and iftex's others are
  real conditionals, answering as pdfTeX does.
- **`\newcommand` is `\long` unless starred**, as in LaTeX; `\def` and
  `\@define` are not. `\@ifstar`, `\@ifnextchar`, `\@ifstrequal`,
  `\ifpackageloaded` and `\iffile` branches may hold paragraphs.
- LaTeX's `list` block (`\begin{list}{label}{settings}`, with `\usecounter`),
  `tabbing` (`\=`, `\>`, `\kill`), `\char`, `\symbol`, `\g@addto@macro`,
  `\@gobblethree`, `\@gobblefour`, `\mathring`, `\dddot`, `\ddddot`,
  `\overleftarrow`, `\overleftrightarrow`.
- LaTeX's page lengths mean what they mean there: `\setlength{\topmargin}`
  moves the text block; `\leftmargin` and `\rightmargin` are list lengths, as
  they are in LaTeX (the page's own margins are now `\@leftmargin` and its
  kin); `\leftmargini`…`\leftmarginiv`, `\marginparpush`, `\footnotesep`,
  `\dblfloatsep`, `\dbltextfloatsep`, `\oddsidemargin`, `\overfullrule` and
  the rest of TeX's missing registers exist.
- nicematrix's `NiceTabular` and matrices, tabularray's `tblr` are read as the
  tabulars and matrices they write out.

### TeX's own rules

- **`\catcode` reaches what is not yet read.** The document is lexed ahead,
  so a category change now rewrites the part of it not yet read, as TeX
  meets characters it has not reached: `\catcode`\!=13` makes every `!` to
  come active, `\catcode`\|=0` makes `|textbf` a command, a comment character
  drops the rest of its line, and a character made a letter carries on the
  control word before it. A change made in a group reaches only to the
  group's end -- its `}`, `\endgroup` or `\end`.
- An active character is a macro only where it is active: a `!` made active
  expands, an ordinary `!` is a `!`; `\def` and `\let` name either. A
  character made ordinary is text whatever it means otherwise:
  `\catcode`\$=12` prints a dollar.
- `\let` and `\csname` share a primitive's identity, not a copy of it, so
  `\ifx` sees them as the same: `\expandafter\ifx\csname undefined\endcsname\relax`
  holds, as it does in TeX.
- `\protected` keeps a macro as written through `\edef`; LaTeX's
  `\DeclareRobustCommand` makes one.
- `\ifvoid`, `\ifhbox` and `\ifvbox` ask after a box register.
- `\count255` and the rest of the 256 registers are there.
- `\@ifnextchar\bgroup` asks after a brace group.
- `@` is an ordinary character in a document, as LaTeX has it: a name
  nothing defines that runs on past an `@` -- `\xymatrix@C=1em`,
  `\ar@{-->}` -- is the name before it and then its characters. The engine's
  own modules, and a document's `\makeatletter` names, keep theirs whole.

### Packages

- **pgfplots draws** (see Pictures).
- **tcolorbox**: `\newtcolorbox{name}[n][default]{options}` makes a box of the
  document's own, its arguments written into its options; `\tcbset` gives
  options every box reads first; `boxrule`, `left`, `boxsep`, `width`,
  `colbacktitle`, `coltitle` and `fonttitle` are read, and a title is set as
  any text is, formulas and commands included.
- **biblatex's styles** are set as the BibTeX style each is nearest:
  `authoryear`, `apa`, `chicago` and `harvard` by author and year, `ieee` as
  IEEE's, `alphabetic` with alpha's labels, `nature`, `science`, the physics
  styles and `sorting=none` in the order cited. `\printbibliography` reads
  `heading=none`, `heading=bibintoc` and `title=`. A report's or a book's
  references are a chapter, `Bibliography`, as those classes have them;
  `apsrev` and the AIP's styles are numbered in the order cited, initials
  first.
- **makeidx and imakeidx set the index**: each `\index{entry}` an anchor
  where it stands, sorted and set as makeindex sets them -- `key@shown`,
  `a!b` a subentry, `|textbf` and `|textit` a page's face, `|see{...}` and
  `|seealso{...}`, a `|(`…`|)` range and three or more pages in a row as one,
  a little space between initials -- in two columns under `\indexname`.
- **acronym**: one used before its list defines it -- the `acronym` block at
  the document's end, as LaTeX's second run reads it -- is spelled out once
  it is; `\acro`'s list is set as a description, and `\acrodef` defines
  without listing. One never defined reads `??`.
- thmtools' `\declaretheorem[keys]{name}` is `\newtheorem` with its title
  from `name=` or the name capitalised, `numberwithin=`, `sibling=`,
  `numbered=no` and `style=`.
- enumitem's `\setlist[kind]{options}`, and `itemsep` and `topsep`.
  `\renewcommand{\labelitemi}{...}` changes a list's bullets.
- titlesec's format reads `\color` and the code after its title:
  `[\titlerule]` draws a rule under the heading.
- `\renewcommand{\familydefault}{\sfdefault}` sets the body, and every
  normal face after it, in sans serif.
- fontawesome5's icons, the letter class, beamer and the index were
  warnings, errors or empty pages before.
- `quantikz`, `forest`, `tikz-qtree`, `qtree`, `xy` (`xypic`), `amscd` and
  `qcircuit` load, drawn as above; tikz-cd, quantikz and forest load TikZ, as
  they do in LaTeX, and quantikz braket's kets.

### Languages

- **babel and polyglossia are packages of their own**, over a native
  `Languages` primitive. `\usepackage[english,russian]{babel}` sets the
  document in the last language named (or `main=`); `\selectlanguage`,
  `\foreignlanguage`, `otherlanguage`, `otherlanguage*` and `hyphenrules`
  choose another; polyglossia's `\setmainlanguage`, `\setotherlanguage(s)`
  give each language a `\textrussian{...}` command and a block of its own
  (Arabic's is `Arabic`, as polyglossia names it).
- **Captions and dates** for 26 languages, from babel's own words: English,
  German, French, Spanish, Italian, Portuguese, Brazilian, Dutch, Russian,
  Ukrainian, Bulgarian, Polish, Czech, Croatian, Hungarian, Romanian, Greek,
  Turkish, Swedish, Danish, Norwegian, Finnish, Catalan, Arabic, Hebrew and
  Persian — `\abstractname`, `\figurename`, `\refname`, `\contentsname` and
  the rest, and `\today` (`28 сентября 2026 г.`). Each is a file under
  `src/modules/babel/`, read the first time the language is chosen.
- **Hyphenation by language**: 78 names babel and polyglossia know, mapped to
  TeX's `hyph-*.pat.txt` and their `\lefthyphenmin`/`\righthyphenmin`.
  Patterns are read the first time a language is chosen; words past ASCII —
  Cyrillic, Greek, accented Latin — are folded to lower case and broken by
  letters, not bytes. A language change applies from where it is written, in
  running text and in boxes alike.
- **Scripts the body's face lacks.** A character Latin Modern cannot draw is
  looked for in New Computer Modern — the same design, with Greek, Cyrillic,
  Hebrew, Armenian, Georgian and Devanagari — in the cut in hand (bold,
  italic, sans, mono), then in the formulas' face for a symbol, then in the
  system's own faces for a script the engine carries nothing for (Arabic,
  CJK). The system's font folders are listed only then, once. Text is cut into
  runs by the face that draws each character and every run shaped whole, so a
  Russian word keeps its kerning and an Arabic one joins; every glyph keeps
  the line height of the face asked for.
- **Right to left.** Arabic, Hebrew and Persian paragraphs read right to left:
  lines start at the right, the first line's indent and a list's margin and
  label are at the right, and a heading's number stands at its title's right.
  Each line is put in drawing order by Unicode's bidirectional algorithm taken
  a word at a time (`Line::reorder`), so a number or a Latin phrase keeps its
  own order, and a run of Arabic inside an English paragraph reads correctly.
  bidi's `\setRTL`/`\setLTR` set the direction directly.
- **TrueType faces are embedded** as `CIDFontType2`/`FontFile2`, which a
  system face — Times New Roman for Arabic — needs.

### Warnings

- `Traceback::Type::Warning`: reported beside errors without failing the run.
  `\warning{text}` is the primitive; `\PackageWarning`, `\ClassWarning`,
  `\PackageError` and `\ClassError` are built on it, and `\PackageInfo`
  writes to the log.
- TikZ keys, values and path pieces the engine does not draw are warnings; the
  picture is drawn without them. Malformed paths are still errors.
- A picture in a form the engine does not draw — PostScript, SVG — is found
  by its extension and its place kept as a framed empty box of the
  size its keys ask for, with a warning. An `\includegraphics` key it cannot
  honour is a warning.
- Text that comes from the engine's own modules or the glossary reports no
  line, rather than a line of a file the document's writer never saw.
- **LaTeX's own words** for what a LaTeX user meets most: `Undefined control
  sequence \foo`; `\begin{a} ended by \end{b}`; `Extra \end{a}`; a document
  ending inside a block, `\begin{a} ended by \end{document}`, or `*** (job
  aborted, no legal \end found)`; ``File `foo.sty' not found``,
  ``File `foo.cls' not found`` and ``File `foo.tex' not found``;
  `No counter 'x' defined`.
- As LaTeX has them, warnings rather than errors: ``Citation `key' undefined``,
  ``Citation `key' multiply defined``, ``Label `x' multiply defined``, and,
  new, ``Reference `x' undefined`` for a reference whose label never comes.

### Typesetting

- A character the text's face cannot draw — ✓, ★, ①, a card suit — is set
  from the formulas' face instead of vanishing; a letter of another script from
  New Computer Modern (see Languages).
- **Fractions clear their bar as TeX's rule 15 has it**, with the font's own
  numbers: a fraction in a line rises and falls by the MATH table's text shifts
  (`FractionNumeratorShiftUp`, `…DenominatorShiftDown`) and keeps the bar's
  thickness clear; a displayed one uses the display shifts and three times the
  thickness; a binomial or `\atop` keeps three or seven times it between its
  parts. The parts used to sit half a point from the bar whatever hung from
  them, so a descender, a limit or a large operator could touch it. The bar is
  as wide as the wider part, with `\nulldelimiterspace` either side.
- A displayed fraction's parts, and a matrix's or `cases`' cells, are in text
  style, as TeX sets them: a sum there is the size it is in a line, with its
  limits beside it, and a fraction inside is a size smaller. `aligned`,
  `gathered` and `dcases` stay displayed.
- TeX's `\over`, `\atop` and `\choose`, written between their parts; `\dbinom`
  and `\tbinom` at their own sizes.
- **Floats are placed by LaTeX's algorithm**: here, top, bottom, or a page of
  floats, as `[htbp!H]` allows, within `\topfraction`, `\bottomfraction`,
  `\textfraction`, `\floatpagefraction`, `topnumber` and the rest (a
  document's own values read at the end), `\floatsep`, `\textfloatsep` and
  `\intextsep` between them; figures and tables each keep their order;
  `figure*` and `table*` span both columns at the head of the next page, their
  captions as wide; `\clearpage` and placeins' `\FloatBarrier` set everything
  waiting.
- **Headings take a look**: titlesec's `\titleformat` and `\titlespacing` set a
  level's face, size, alignment, capitals, label and spacing, over a native
  `\@heading`; IEEEtran (`I. INTRODUCTION` in small capitals, `A. Method` in
  italic), acmart, llncs, elsarticle, revtex4-2, amsart, svjour3 and
  KOMA-Script set theirs.
- `\setmainfont` and `\setmathfont` set the nearest carried face: STIX Two for
  Times and its kind, New Computer Modern, Latin Modern, the TeX Gyre maths.
- The page builder was rewritten; two-column pages, `\twocolumn[...]`,
  `\onecolumn` and `multicols` balance their columns; TeX's glue registers
  (`\newskip`) hold stretch and shrink; tables, headings, lists, citations
  (natbib and biblatex styles, author–year labels), counters and numerals,
  groups, colours and TikZ pictures (paths, nodes, grids, circles, arcs,
  labels, colours) were extended through the cycle, and appendix B of
  `build/main.mtex` checks them on the page.
- A math argument may be empty: `\frac{}{2}`, `\underset{}{=}`.
- Wide accents and arrows over a symbol (`\widehat`, `\widetilde`,
  `\overrightarrow`) now draw their marks.
- **Paragraphs break as TeX breaks them**: a pass without hyphenating up to
  `\pretolerance` (100), then one hyphenating up to `\tolerance` (200), then
  an emergency pass; Knuth's demerits with `\doublehyphendemerits`,
  `\finalhyphendemerits` and `\adjdemerits` across fitness classes.
- **Space after a sentence** is TeX's: the space factor of `.`, `?` and `!`
  (3000), `:` (2000), `;` (1500) and `,` (1250) widens and loosens the space
  after it, and a capital before the full stop keeps it an abbreviation's;
  `\frenchspacing` and `\@` as LaTeX has them.
- **A paragraph's shape is its group's**: `{\centering ...\par}` and
  `{\raggedleft ...\par}` shape the paragraphs ended inside the group and no
  others; the shape used to run on to the end of the document.
- **Formulas take the text's size and colour**: `{\small $x$}` is small, a
  heading's formula a heading's size, and `\textcolor{red}{$x+y$}` red, its
  rules too. A rule takes the colour of the text around it.
- `\\[20pt]` and `\newline[...]` leave the space they ask for under their line.
- **Vertical fill works**: `\vfill`, `\vspace{\fill}` and `\vspace{\stretch{2}}`
  share out what a page leaves, and `\vspace*` keeps its space at a page's
  head, behind a rule of no height, as LaTeX's does.
- A page holding only instructions -- a numbering change, a contents entry --
  between two breaks is no page; they go to the next, as TeX ships none.
- `\setcounter{page}{n}` numbers the page it lands on.
- A TikZ picture stands in the line, as LaTeX's boxes do: two side by side
  with `\hfill` between, one centred by `\centering`.
- The logos drawn as LaTeX draws them: `\TeX`, `\LaTeX`, `\LaTeXe`.
- A displayed equation too wide for its number moves left, or puts its
  number on the line below, as amsmath does.

### Pictures

- A JPEG in grey or colour is embedded as it came (`DCTDecode`): only its
  header is read, nothing decoded or compressed again.
- A picture's natural size follows the resolution its file gives — PNG's
  `pHYs`, JPEG's JFIF density — at 72 dpi when it gives none, as pdfTeX has it.
- **A PDF page is a picture**: `\includegraphics{figure.pdf}` embeds the page
  itself as a Form XObject, fonts and vectors as they were, with `page=`,
  `trim=`, `viewport=` and `clip`. Classic cross-reference tables, PDF 1.5
  cross-reference and object streams, and a file whose table lies are read;
  an encrypted one is refused.
- **TikZ curves and fills**: `.. controls ..`, `to[bend left]`,
  `to[out=,in=,looseness=]`, `\fill`, `\filldraw`, `fill=` and `draw=`.
- **TikZ nodes as flowcharts use them**: `draw`, `fill`, `circle`,
  `ellipse`, `rounded corners`, `inner sep`, `minimum width` and `height`,
  `text width` (the text set as a paragraph), `font`, `align`; positioning's
  `right=of a` with `node distance`; anchors; `midway` and `near start`;
  lines stopping at a node's border, `|-` and `-|` included; `\coordinate`;
  styles by name with `\tikzset` and `\tikzstyle`, `/.style` and
  `/.append style`; `\foreach` with `...` ranges, `count=` and several
  variables; arrow tips written TikZ's way, `-{Latex[length=2mm]}`.
- **TikZ plots**: `plot coordinates {...}` and `plot ({\x}, {\x*\x})` over
  `domain` in `samples` steps, the function worked out as pgfmath works one
  -- trigonometry in degrees, `r` for radians.
- **pgfplots axes are drawn**: `axis`, `semilogxaxis`, `semilogyaxis` and
  `loglogaxis`; ticks at round numbers no more than about 35 points apart,
  powers of ten on a logarithmic axis, or the ones `xtick` lists; labels,
  title, `grid`, `axis lines=left` and `middle`; `\addplot` of coordinates,
  of a table inline or in a file, of a function of x or two (parametric),
  clipped to the box; pgfplots' own colours and marks for a plot given none,
  `only marks`, `mark=`, dashes and widths; `ybar` with `symbolic x coords`
  and `xtick=data`; `enlargelimits`; a width or height alone scaling the
  other; `\legend` and `\addlegendentry`, in the corner `legend pos` names.
- **Commutative diagrams are drawn**, each as the TikZ picture it is: a
  matrix of its objects and an arrow for each, its labels on the side the
  package puts them. tikz-cd's `\arrow` -- `"f"`, `"f"'` swapped,
  `description`, `hook`, `tail`, `two heads`, `mapsto`, `dashed`,
  `Rightarrow` and `equal` as double lines, `bend left`, `shift left`,
  `phantom`, `from=`, spacing by tikz-cd's names; xy-pic's `\xymatrix`, with
  `^`, `_` and `|` labels, `@{-->}` shapes, `@<1ex>` shifts, `@/^/` curves
  and `@C=`/`@R=`; amscd's `CD`, its `@>>>`, `@VVV`, `@<<<`, `@AAA` and `@=`.
  A display holding one alone is set as the picture, its number level with
  its middle, as tikz-cd sets a diagram on the formula's axis; one in a
  formula of other things is the grid of its objects.
- **Circuits**: quantikz's block and Qcircuit's `\Qcircuit` -- a wire a row,
  gates boxed on it, `\ctrl` joined to its `\targ`, open controls, swaps,
  meters, classical wires doubled, `\lstick` and `\rstick`.
- **Trees**: forest's `[S [NP] [VP]]`, and qtree's and tikz-qtree's
  `\Tree [.S the cat ]` with bare words as leaves, each parent centred over
  its children.
- **TikZ's `\matrix`**: `matrix of nodes` and `matrix of math nodes`, or a
  `\node` a cell; cells named `m-1-2`, each column as wide as its widest and
  each row as high as its highest, `row sep`, `column sep`, `\\[4pt]`,
  `|[draw]|`, `nodes=`, `ampersand replacement`, `draw` and `anchor`.
- **A label stands where TikZ puts it**: one written on a line, `(a) --
  node {x} (b)`, halfway along it -- it stood at the line's start --
  `pos=`, `near start`, `at end` and the rest, along a curve too; `auto` on
  the line's left and `swap` its right.
- **TikZ's `edge`**: each from where the path stands with its own options --
  tips, bends, a `loop above` back to its node -- the path going on from
  where it was, as automata are drawn. `out=`, `in=` and a bend leave and
  meet a node's outline at their angles; a bend given the path is every
  `to`'s. `double` lines, tikz-cd's `shift left`, and tips `|`, hooks,
  `>>` and tails.
- **A picture stands on the baseline**, as TikZ's and LaTeX's do: its bottom
  there, or the height `baseline=` names -- a length, or a node's centre,
  top or bottom, `([yshift=-.5ex]a.center)`. It used to hang below the line.
  `\tikz[options]{...}` and `\tikz \draw ...;` both read.
- Lengths in picas, `dd` and `cc`, in a picture's options.

### The file

- **hyperref's links**: with hyperref loaded, a reference, a citation, an
  entry of the contents, `\href`, `\url`, `\hyperref[label]{...}` and
  `\hyperlink` go where they say when the file is read -- framed as hyperref
  frames them (red within the document, cyan an address, green a citation),
  in `linkcolor`, `urlcolor` and `citecolor` with `colorlinks`, or plain with
  `hidelinks` or `pdfborder={0 0 0}`. A link broken over two lines is two
  areas, and one over a page's end is on both pages.
- **Bookmarks**: every heading hyperref would bookmark is in the file's
  outline, one level under another, and the file opens with it shown.
- **The file's information**: `/Producer`, and the `pdftitle`, `pdfauthor`,
  `pdfsubject` and `pdfkeywords` \hypersetup gives; an `/ID` made from the
  file's own bytes, the same for the same document every time.
- **PDF/A-2b**, the archival standard, for pdfx's `a-2b` (and its kin) or
  `\DocumentMetadata{pdfstandard=A-2b}`: the file's information again as XMP,
  and an output intent with an sRGB ICC profile made by the engine; every
  face is embedded already, and every link marked to print.

### For programs

- `engine::Session`: values, commands, packages and pictures handed in once
  and kept across documents; `set`, `define`, `provide`, their undoing verbs,
  `typeset`, `pdf`, `error`, and `pages()` for each page's text.
- The same interface as a C library (`network/ffi.hpp`) and as a WebAssembly
  module (`render/wasm.hpp`).
- The command line takes `--set=name=value`.

### Speed

Release build, clang-cl, one run each, median of thirty:

| Document | Pages | Time |
| --- | --- | --- |
| a one-line article | 1 | 3.2 ms |
| a Russian article with babel, its contents and a bibliography, two passes | 1 | 4.6 ms |
| two tikz-cd diagrams, an xy-pic and an amscd one, a matrix, an automaton, a circuit and two trees | 1 | 4.7 ms |
| an Arabic article with polyglossia, right to left | 1 | 5.8 ms |
| the paper in `build/main.mtex` | 8 | 11.9 ms |

Rewriting a category change into the tokens ahead costs nothing until a
document makes one; links, bookmarks and the index are made only for a
document that asks for them.

The glossary costs a flag per unknown name and one lookup the first time;
modules and English's hyphenation patterns are compiled in, another
language's read when it is chosen; a JPEG is never decoded; the system's
font folders are listed only for a script the engine carries no face for.

### Fixed

- A primitive that bound another while it ran — `\newif` making its switch —
  could grow the expander's handler table under itself and run on from freed
  memory. Each handler, in the expander and in the parser, now lives in an
  allocation of its own.
- `\numexpr` let a `\relax` that followed a conditional's `\fi` be swallowed,
  so `\numexpr 3+\ifnum…\fi\relax` read past its end.
- `\unexpanded\expandafter{…}` took the `\expandafter` as its text; what
  stands before the brace is now expanded first, as e-TeX has it.
- The logger's floor could differ between the engine and a program linked
  against it; a release engine now tells everything linking it
  (`LATEX_RELEASE`).
- A package's accumulating lists (`authblk`'s affiliations) referred to
  themselves unexpanded and looped when set.
- A fraction's numerator or denominator could touch its bar (see Typesetting).
- `\includegraphics` kept the pictures it had read where a later one could
  move them; each is now kept where it stays.
- Text in a box, a minipage or a table's cell was shaped whole and never
  hyphenated; it is set as running text is, sharing its cache of words.
- `{\centering ...}` and `\raggedright` inside a group ran on past it, to the
  end of the document.
- A page holding nothing but a numbering change or a contents entry was
  shipped as a blank page.
- `\\[len]` dropped its length.
- A `\catcode` change reached only text lexed after it, which was none of
  the document's; `\ifx` told a `\let` copy of a primitive from the
  primitive; an ordinary `!` ran the macro an active `!` had.
- A formula in `\small` text, or in coloured text, was set at the body's
  size in black.
- A tcolorbox module's definitions hid the native `\newtcolorbox`, and a
  title with a formula or a command in it was set as plain words.
- A TikZ label written on a line stood at the line's start, and `midway`
  on a curve stood by its end.
- A picture in a line hung below the baseline; `\tikz[baseline]{...}` read
  its bracket as the picture; a length in picas was none.

### Tests

- Every test is one standalone `tests/<path>/test_<file>.cpp` for a real
  `src/<path>/<file>.cpp`, with its own small harness and plain `assert`; the
  shared helper headers are gone. 83 tests pass in Debug and Release.
- New: `test_glossary`, `test_drawing`, `test_languages`, and cases for
  classes, fontspec, filecontents, tabbing, `list`, `.bbl` bibliographies,
  kept picture places, JPEG and PNG resolution, aliases, a primitive that grows
  the table under itself, float placement, PDF figures, TikZ curves and fills,
  fractions' clearance from their bar measured on the page, `\over`, matrix
  cells in text style, a face for another script, the system's folders,
  hyphenation by language, and lines put in drawing order.
- Then: `test_plots`; category changes ahead of the reader, `\count255`,
  `\csname` and `\ifx`, `\protected`, `\@ifnextchar\bgroup`; `\name` and
  `\endname` for a block, an undefined one's warning; beamer, the letter
  class, `titlepage` and `\setcounter{page}`; an alignment kept to its group;
  beamer's blocks and columns, `\ifvoid`, `\newtcolorbox` and `\tcbset`;
  titlesec's colour and rule, `\frontmatter`, the index, bookmarks, `acks`;
  `\@forward`, links, `hidelinks`, none without hyperref, `Reference
  undefined`; a coloured rule; a TikZ `plot`, pictures side by side;
  biblatex's `ieee` and `title=`, a report's `Bibliography`, a citation's
  link; `teaserfigure`, `CCSXML`; PDF/A.
- Then: `test_diagrams` -- tikz-cd, xy-pic and amscd diagrams, a numbered
  one's number at its middle, circuits, trees; and a picture on the
  baseline, `\matrix`, a label halfway along its line, edges and loops.

### Documentation

- `docs/packages.md`: where a package is found (aliases, provided packages,
  warnings), the glossary, classes (beamer, letter, `\@bodyshape`,
  `\@bibkind`), long definitions, characters and category changes, links and
  text given later, and environments as native blocks. The Doxygen reference
  builds with no warnings.

### Names

- **No name carries an underscore.** A list a class keeps and hands out
  read-only is the plural, and what hands it out the singular: every module's
  `tracebacks` and its `traceback()`, the wrappers' `traceback()` merging
  them. A field an accessor only returned or set is the field itself, public:
  `mouth.lexicon`, `mouth.arena`, `mouth.state`, `mouth.error`,
  `mouth.vertical`, `parser.mouth`, `state.registers`, `state.catcodes`,
  `registers.quad`, `node.type`, a traceback's `type`, `location` and
  `message`, an image's `width`, `pixels` and `resolution`, a drawing's
  `box` and `objects`, the document's `configuration`, `furniture`,
  `metadata` and `fallback`, the composer's `pictures`, `faces`, `anchors`
  and the rest, a session's `pdf`, `error`, `pages`, `host` and `assets`,
  and the core wrapper's `relay`, `blocks` and `variables` (which were
  `conditionals()`, `structure()` and `variables()`). A field a parameter
  would shadow takes its role: `\par`'s symbol is `paragraph`, the robust
  switch `keeping`, the composer's colours in use `filling` and `stroking`.
- **One verb for each thing done.** Every lookup is `get` -- the catalog's,
  the glossary's, the modules', the ledger's, a language's, the symbol
  table's (gperf's `Lookup::get`), a variable's -- and every store `set`
  (the ledger's); what `compose` starts `dispose` ends, the logger included
  (`Logger::dispose`, which was `close`); the expander's `forget` takes back
  what `define` gave, as the language's `\forget` does (`undefine`).
- `CatCodes` is `Catcodes`, and the last snake_case names (in the plots
  module) are single words.
- `#embed` needs no pragmas: Clang's claim that it is an extension under
  C++26 is turned off once in CMakeLists.txt, and the
  `#if defined(__clang__)` blocks around the three files that embed are gone.

### Removed

- The old `render/primitives/structure`, `configuration` and `expression`
  folders, replaced by one flat module per concern; fontconfig lookup, in
  favour of the font library over `assets/fonts`; margin protrusion; the
  syntax cache; checked-in PDFs and `build/main.tex`.

### Not yet

EPS and SVG are placeholders, not drawn; TikZ's `let` is a warning;
pgfplots' 3D plots, error bars and `fill between` are not drawn; a tree's
labels are measured by their letters, not set first; xy-pic's objects
written with `*+[F]` keep that text. beamer's overlays are one slide and
its themes are read, not drawn. A `\chardef`'d character is a number, not a
glyph. By choice, never: `\write18`, reading a `.sty` or `.cls` from disk,
and a second run's `.aux` -- the engine's one pass does what that run is for.
Languages: no face for Arabic ships with the engine, so it needs one
on the system (Windows and the Mac have one; a Linux without Noto or Amiri
does not); digits stay Western; French spacing before `:;!?`, Arabic's
kashida and CJK line breaking are not done; a box's own lines — a minipage in
Arabic — are not yet reordered. Coverage figures above count packages that
load and whose common commands set text — not that every feature of each is
drawn.
