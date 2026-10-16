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
- **509 packages and 14 classes, each proven by a document of its own**
  (was about 360, and 121 before that): `tests/packages/<name>.mtex` uses
  what its package is loaded for, and the engine's test sets all 523 and fails
  on any error, any unknown package and any command nothing defines. New
  folders this round: `CJK`, `SIunits`, `acro`, `axodraw2`, `bohr`,
  `changepage`, `commath`, `dirac`, `elements`, `emoji`, `environ`, `feynmp`,
  `fmtcount`, `modiagram`, `newfloat`, `ntheorem`, `outlines`, `paracol`,
  `pgf`, `pgfgantt`, `pst-node`, `pstricks`, `scrlayer-scrpage`, `sidenotes`,
  `smartdiagram`, `tabu`, `tasks`, `textpos`, `thmtools`, `threeparttablex`,
  `tikz-feynman`, `tipa`, `titleps`, `tkz-euclide`, `totpages` and `xifthen`;
  the rest are glossary lines, aliases or provided names. Before that, about
  360 of the 394 most-used CTAN packages loaded:
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
  `\tableofcontents`, and `\AtBeginSection`'s frames. Templates are read.
- **beamer's themes are drawn**: `\usetheme` -- Madrid, AnnArbor,
  Boadilla, CambridgeUS, Warsaw, Copenhagen, Luebeck, Malmoe, Berlin,
  Darmstadt, Ilmenau, Frankfurt, Dresden, Singapore, Szeged, Antibes,
  JuanLesPins, Montpellier, Rochester, Pittsburgh, the sidebar themes and
  metropolis -- as an outer theme's head and foot at the slide's edges (infolines' author, title, date and page; split's
  author and title; the
  section's and subsection's names above), a frame title on a band or in
  the structure's colour, an inner theme's bullets (triangles, balls,
  circles, squares) and a colour theme's colours (whale, dolphin,
  seahorse, beaver, crane, wolverine, spruce, dove, seagull, fly, ...);
  `\useoutertheme`, `\useinnertheme` and `\usecolortheme` alone too, a
  theme it does not know drawn as the default with a warning. A title,
  author and date's short forms, `\title[Short]{...}`, are what the foot
  carries. A sidebar theme's sidebar is not drawn.
- **beamer's overlays**: a frame is as many slides as its overlays ask for,
  each a page. `\pause` (and `\pause[3]`) hides the rest of the frame until
  its step; `\only<2>{...}` is there on its slides alone; `\uncover`,
  `\visible` and `\onslide<2->{...}` are in their place on every slide and
  seen on theirs, `\invisible` the other way round, and `\onslide<2->`
  alone sets what follows it; `\alt<2>{a}{b}`, `\temporal`; `\item<2->`
  with its label, and a list's default, `\begin{itemize}[<+->]`, one item
  a step; `\alert<2>`, `\textbf<2>`, `\textit`, `\emph`, `\structure`,
  `\color` and `\textcolor` on their slides; `onlyenv`, `uncoverenv`,
  `visibleenv`, `invisibleenv`, and a block or a theorem given an overlay.
  Specifications as beamer reads them: `2`, `2-`, `-3`, `1-3,5`, `+-`,
  `.-`, `+(1)-`. What a slide does not show keeps its place, unseen and
  out of its page's text, so the slides of a frame line up.
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
- **A `.bib` file imports as any file is input.** `\input{refs.bib}` (or
  `\include`) makes it a source of the bibliography, as biblatex's
  `\addbibresource` does, rather than reading BibTeX's `@entries` as TeX;
  `\bibliography{}` and `\printbibliography` both set what it holds, and a
  file `\bibliography` names as well is read once. One that is not there is
  ``File `refs.bib' not found``.
- **A `.bib` file is a document of its own**: `latex refs.bib` -- or
  `latex refs`, a `.bib` looked for after `.mtex` and `.tex` -- sets every
  entry it holds, cited or not, in plain's style under an article's
  References, to look a bibliography over before citing it.
- **`@preamble`** is kept, as BibTeX writes it into the `.bbl`: its text
  goes ahead of the list, so a command the entries use -- `\noopsort`, a
  logo -- is defined before they are set. It was read past.
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

- **A `\chardef`'d name is its character** where text is written --
  `\chardef\x=65 a\x b` sets aAb -- and its number wherever one is read,
  as TeX's is. It was a number everywhere, and an assignment in text.
- **`\catcode` reaches what is not yet read.** The document is lexed ahead,
  so a category change now rewrites the part of it not yet read, as TeX
  meets characters it has not reached: ``\catcode`\!=13`` makes every `!` to
  come active, ``\catcode`\|=0`` makes `|textbf` a command, a comment character
  drops the rest of its line, and a character made a letter carries on the
  control word before it. A change made in a group reaches only to the
  group's end -- its `}`, `\endgroup` or `\end`.
- An active character is a macro only where it is active: a `!` made active
  expands, an ordinary `!` is a `!`; `\def` and `\let` name either. A
  character made ordinary is text whatever it means otherwise:
  ``\catcode`\$=12`` prints a dollar.
- `\let` and `\csname` share a primitive's identity, not a copy of it, so
  `\ifx` sees them as the same: `\expandafter\ifx\csname undefined\endcsname\relax`
  holds, as it does in TeX.
- `\protected` keeps a macro as written through `\edef`; LaTeX's
  `\DeclareRobustCommand` makes one.
- `\ifvoid`, `\ifhbox` and `\ifvbox` ask after a box register.
- `\count255` and the rest of the 256 registers are there.
- `\@ifnextchar\bgroup` asks after a brace group.
- An internal integer or length -- a register's name, a `\newbox`'s, a
  `\chardef`'s -- takes no optional space after it, as in TeX: only digits
  and a unit do. `\usebox{\held} and` kept no space before its word.
- Each of several category changes in a row carries on the control word
  before the characters it makes letters, not only the first.
- `\the\value{counter}` prints the counter, LaTeX's `\value` being the
  register it names.
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
- **xstring** is native (`syntax::primitives::Strings`): `\IfSubStr`,
  `\IfBeginWith`, `\IfEndWith`, `\IfStrEq`, `\IfEq`, `\IfInteger`,
  `\IfDecimal`, `\StrLen`, `\StrLeft`, `\StrRight`, `\StrMid`, `\StrChar`,
  `\StrGobbleLeft` and `Right`, `\StrBefore`, `\StrBehind`, `\StrBetween`,
  `\StrSubstitute`, `\StrDel`, `\StrCount` and `\StrPosition`, each worked out
  in one pass on the expanded text, UTF-8 characters counted whole, a result
  set where it stands or kept in the macro named after it.
- **csvsimple and datatool** are one native module (`syntax::primitives::Records`): `\csvreader` with
  `head to column names`,
  `tabular=` and `table head=`, `\csvautotabular`, `\csvautobooktabular`;
  `\DTLnewdb`, `\DTLnewrow`, `\DTLnewdbentry`, `\DTLloaddb`, `\DTLforeach`,
  `\DTLdisplaydb`, `\DTLrowcount`, `\DTLcolumncount`, `\DTLgetvalue`,
  `\DTLfetch`, `\DTLsort` and `\DTLsumcolumn`; quoted fields, and commas,
  semicolons or tabs between them.
- ifthen's tests are native (`\@iftest`): numbers compared, `\equal`,
  `\boolean`, `\isodd` and `\lengthtest`, joined by `\AND` and `\OR`, turned
  by `\NOT` and grouped by `\(` and `\)`; `\ifthenelse` and `\whiledo` are
  made of it, and xifthen adds `\isempty`, `\isin` and `\isnamedefined`.
- Numbers in words: fmtcount's `\numberstringnum`, `\ordinalstringnum`,
  `\numberstring{counter}` and their capitalised kin, from a native
  `\@numberwords` (`Numeral::words`).
- **expl3, the basics** (`core/expl3.mtex`): `\ExplSyntaxOn` and `Off`;
  `\cs_new:Npn`, `\cs_set:Npn` and their protected, `nopar` and `_eq`
  forms; token lists (`\tl_new:N`, `\tl_set:Nn`, `\tl_put_right:Nn`,
  `\tl_if_empty:nTF`, …), integers (`\int_set:Nn`, `\int_incr:N`,
  `\int_compare:nTF`, `\int_to_roman:n`, …), booleans, `\str_if_eq:nnTF`,
  `\str_case:nn`, `\fp_eval:n`, `\keys_define:nn` and messages -- enough for
  a preamble's own LaTeX3 macros.
- cleveref's lists: `\cref{sec:a,sec:b}` reads `sections 1 and 2`, labels of
  different kinds each named in turn, as cleveref has them. A reference
  nothing defines is reported once the document's end code has run, so
  lastpage's `\pageref{LastPage}` resolves.
- fancyvrb's `Verbatim*` and `BVerbatim`, `spverbatim`, `boxedverbatim`, and
  luacode's `luacode` and `luacode*` (read and not run) are verbatim blocks;
  `\Verb` is `\verb`. url's `\path` outside a picture.
- newfloat's `\DeclareFloatingEnvironment` and the float package's
  `\newfloat` make a float kind of the document's own, its caption named.
- xfp and l3fp's `sqrt`, `abs`, `round`, `floor`, `ceil`, `trunc`, `min`,
  `max`, `pi` and `e`; pgfplotstable's `\pgfplotstabletypeset` sets a table
  from its file; tabularray's `colspec=`; `xltabular`.
- An unknown `\pagestyle{name}` runs `\ps@name`, as LaTeX's does, so a
  package's own page styles work; `\parindent` is a real register.
- An environment nothing else defines is read from the glossary's `\name` and
  `\endname` lines; iftex's `\ifluatex`, `\ifxetex` and `\ifpdftex` are false
  as pdfTeX's would be in this engine; `\smash` in text; `\-`.

### Languages

- **The engine carries an Arabic face**, Noto Naskh Arabic in its regular
  and bold cuts (SIL Open Font License, its text beside the fonts in
  `assets/fonts/text`), and draws Arabic, Persian and Urdu with it before
  looking at the system's faces: an Arabic document is the same on every
  system, a Linux with no Arabic face of its own included.
- **A language's own digits**: Arabic's ٠١٢٣ and Persian's and Urdu's ۰۱۲۳
  for the digits typed, a number read left to right in them, as
  polyglossia's mapping draws them -- the digits as typed again for
  `\setmainlanguage[numerals=maghrib]{arabic}`, polyglossia's options now
  reaching the engine (`\@language[options]{name}`).
- **French's spaces**: babel's and polyglossia's French sets a space no line
  ends at before `;` `!` `?` and `»` and after `«` -- half a word space --
  and a word space before `:`, taking in the space typed there, and spaces a
  stop as any other mark; a colon in a time or an address is left be.
- **Chinese and Japanese break between characters**: a line may end between
  any two, written without spaces as they are, at a glue of no width that
  stretches a little when the line is justified (xeCJK's `\CJKglue`) --
  but before no closing mark, small kana or long vowel and after no opening
  bracket, the kinsoku rules; a Latin run among them is kept whole. A line
  of them used to run on to the next space.
- **Arabic is justified with kashida**: where a justified line has room to
  spare, one join in each Arabic word is drawn longer -- after seen, sheen,
  sad or dad, else before a closing heh, teh marbuta, dal, reh, waw or alef,
  else at the word's last join -- as a stroke of the face's own tatweel,
  rather than its spaces alone widened. No line ends there, a word read
  right to left keeps it between the letters it joins, a word in a color
  draws it in that color, and a word written with its vowels is left as it
  is.
- **A box reads the way its text does.** A minipage, a `\parbox` and a
  framed block begun in Arabic or Hebrew set their paragraphs right to left,
  each line in the order it is drawn in, and a direction changed inside one
  holds to its end; their lines were left in the order they were written.
- **A table in Arabic or Hebrew runs right to left**: its first column at
  the right, each vertical rule and padding mirrored, a short row's cells
  from the right and a rule over some columns over their mirror, as a table
  set under polyglossia is. A cell's paragraph reads the table's way, and a
  one-line cell is put in drawing order, so Arabic in a cell reads right to
  left in any table.
- **babel and polyglossia are packages of their own**, over a native
  `Languages` primitive. `\usepackage[english,russian]{babel}` sets the
  document in the last language named (or `main=`); `\selectlanguage`,
  `\foreignlanguage`, `otherlanguage`, `otherlanguage*` and `hyphenrules`
  choose another; polyglossia's `\setmainlanguage`, `\setotherlanguage(s)`
  give each language a `\textrussian{...}` command and a block of its own (Arabic's is `Arabic`, as polyglossia names
  it).
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
- **A word in two faces reads right to left whole**: `والفلسفة.`, its
  letters the Arabic face's and its stop the text face's, ended a sentence
  with the stop at the word's right, where the face's run left it; the stop
  now stands at its left, and a bracket the text face set is drawn facing
  the other way when it reads right to left, so `(كلمة)` looks as it is
  written. A word in two faces with nothing right to left in it, `x²`,
  is kept whole.
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
- **A glyph's ink is what its outline draws**, not the box its file keeps for
  it: a contour of one point, an anchor some faces keep far above or below a
  glyph, no longer counts as ink. Noto Naskh's tatweel measured from under
  the baseline to over the x-height that way; it is the thin stroke on the
  baseline it draws. A face with no such point measures as it did.
- **Fractions clear their bar as TeX's rule 15 has it**, with the font's own
  numbers: a fraction in a line rises and falls by the MATH table's text shifts (`FractionNumeratorShiftUp`,
  `…DenominatorShiftDown`) and keeps the bar's
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
  `\onecolumn` and `multicols` balance their columns; TeX's glue registers (`\newskip`) hold stretch and shrink; tables,
  headings, lists, citations (natbib and biblatex styles, author–year labels), counters and numerals,
  groups, colours and TikZ pictures (paths, nodes, grids, circles, arcs,
  labels, colours) were extended through the cycle, and appendix B of
  `build/main.mtex` checks them on the page.
- A math argument may be empty: `\frac{}{2}`, `\underset{}{=}`.
- Wide accents and arrows over a symbol (`\widehat`, `\widetilde`,
  `\overrightarrow`) draw their marks in the font's first width that spans
  what they cover (`typography::Expression::widen`, the MATH table's
  horizontal variants); every accent is centred over its letter by the mark's
  ink, not its advance, so a combining mark no longer leans onto the letter
  before it.
- **Braces over and under**: `\overbrace` and `\underbrace`, and
  `\overbracket`, `\underbracket`, `\overparen` and `\underparen`, drawn the
  width of what they span, tapering as a pen's stroke does, with the label
  written as a script set over or under them as a limit, in a line as in a
  display.
- **Paragraphs break as TeX breaks them**: a pass without hyphenating up to
  `\pretolerance` (100), then one hyphenating up to `\tolerance` (200), then,
  when neither keeps every line inside the column, one with
  `\emergencystretch` (3em) credited to every line; Knuth's demerits with
  `\doublehyphendemerits`, `\finalhyphendemerits` and `\adjdemerits` across
  fitness classes. The last pass used to take any line up to badness 2000,
  so a narrow column -- a `p{3cm}` cell -- ran a word into its rule rather
  than set a looser line.
- `\parbox[t]` and `[b]`, and a minipage's, stand on their first or last
  line's baseline: the line was taken to be the paragraph, so a box hung by
  its first paragraph's whole height.
- tabbing's stops stand past the space typed before `\=`, as LaTeX's do, and
  a line `\kill` ends is measured and not drawn -- in a tabbing, a tabular or
  a longtable alike. It used to be printed, its columns glued together.
- `\` ending a source line is a word space in a formula too, as `\ ` is.
- **A ragged line is filled before it breaks**: every line of a ragged
  paragraph costs the same, and a tie now goes to the later break, as
  TeX's does, so `\raggedright`, a centred block and a beamer slide fill
  each line in turn; a paragraph of two lines broke at its first chance,
  a word or two on its first line.
- **Text a command sets is in the colour in use**: `\textcolor{red}{50\%}`'s
  sign, an accented `\'e`, a `\ldots` -- each was set black whatever the
  colour around it.
- **`\headsep` and `\footskip` place the head and the foot**: set with
  `\setlength`, they say how far above the text a head's baseline stands
  and how far below it a foot's; both were fixed at article's 25 and 30
  points, a head or a foot off the page of a slide.
- **Marks**: `\markboth{left}{right}` and `\markright{right}` set what a
  head's or a foot's `\leftmark` and `\rightmark` show, on the page they
  are written on and the next until another is set; both showed nothing.
- **A colour chosen in a box stays in it**: `\mbox{\color{red}x} y`,
  `\textbf{\color{blue}x}`, a TikZ node's, a list label's, a cell's, a
  footnote's -- what followed the box took the colour, as only the face
  was put back after it.
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
- **A fraction in a line has room to read.** Its numerator and denominator
  are a size down, as TeX sets them, but keep the space round their signs
  and relations at that size where TeX closes `-b \pm \sqrt{b^2-4ac}` up to
  nothing; an exponent or a subscript stays as tight as TeX's. Its parts
  stand halfway from the font's least gap to its display gap from the bar,
  where TeX's one rule's width left a root's sign resting on it.
- **`standalone` crops its pages.** `\documentclass{standalone}` sets its
  text as article does, with no indentation and nothing in its heads or
  feet, on a sheet as tall as a PDF's page may be, and each page's box is
  then what is drawn on it with `border=` round it -- half a point unless the
  class's options say otherwise. The composer measures what every page puts
  ink on as it draws it -- glyphs, rules, strokes, pictures, a rotated or
  scaled box by its corners -- and any other class keeps the whole sheet.

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
- **A node's own outline**: `\node[draw, dashed]`, `dotted`, `double` -- a
  second outline two points out -- and `rounded corners=3pt`, each corner
  a quarter circle; a node's outline was solid and square whatever it said.
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
- **Lengths in a formula**: a number with a unit, `1cm`, `3pt`, `2em`, is a
  length in points, and a formula that holds one is a length, as pgfmath
  has it -- `\x1+1cm`; `veclen`, `mod`, `int` and `sign` are worked out
  with the rest.
- **A TikZ coordinate may be formulas**: `({1+1},{sin(30)*2})`, a polar
  `({atan2(1,2)}:{veclen(3,4)})`, a length anywhere one is read; parentheses
  and braces nest inside a coordinate, which used to end at its first `)`.
  `([xshift=10pt]b)` and `yshift` move a point or a node's anchor.
- **TikZ's calc library**: a coordinate `($...$)` worked out of others --
  added and taken away, `($(a)+(0,1)$)`, times a factor, `($2*(b)$)`,
  partway along, `($(a)!0.5!(b)$)`, turned first, `($(a)!0.5!90:(b)$)`, or
  a length along, `($(a)!1cm!(b)$)`.
- **TikZ's `let`**: `\draw let \p1 = (a), \p2 = (b), \n1 =
  {veclen(\x2-\x1,\y2-\y1)} in (a) circle (\n1);` -- point registers and
  each one's `\x` and `\y`, number registers, names in braces, `\p{top}`,
  and each register using those set before it -- written out in the rest
  of the path. It used to be a warning and nothing drawn.
- **pgfplots axes are drawn**: `axis`, `semilogxaxis`, `semilogyaxis` and
  `loglogaxis`; ticks at round numbers no more than about 35 points apart,
  powers of ten on a logarithmic axis, or the ones `xtick` lists; labels,
  title, `grid`, `axis lines=left` and `middle`; `\addplot` of coordinates,
  of a table inline or in a file, of a function of x or two (parametric),
  clipped to the box; pgfplots' own colours and marks for a plot given none,
  `only marks`, `mark=`, dashes and widths; `ybar` with `symbolic x coords`
  and `xtick=data`; `enlargelimits`; a width or height alone scaling the
  other; `\legend` and `\addlegendentry`, in the corner `legend pos` names.
- **pgfplots' error bars**: `error bars/.cd, y dir=both, y explicit` with
  coordinates written `(1,2) +- (0,0.25)`, `+=` above alone and `-=`
  below; a table's `y error=` column, by name or `y error index`, or `y
  error plus` and `minus`; one amount for every point, `y fixed=0.1`, or a
  share of each, `fixed relative`; across with `x dir`. Each is a line in
  the plot's colour with a bar across each end, and the axis's limits make
  room for them.
- **pgfplots' `fill between`**: `\addplot[gray!30] fill between[of=f and
  g];` fills the region between two plots named with `name path`, under
  every line, kept to `soft clip={domain=a:b}` with each curve's end where
  it crosses an edge; `fill opacity` pales it, a plot given none takes
  pgfplots' next colour paled, and the legend shows a patch. A curve drawn
  with `draw=none` is named for a fill to reach and not drawn.
- **pgfplots in three dimensions**: `\addplot3` of coordinates `(x,y,z)`, a
  table's x, y and z, a function of x and y -- `{x^2 - y^2}` over `domain`
  and `y domain`, `samples` by `samples y` -- or three functions, a curve
  with `samples y=0`. The axis is seen from its `view={25}{30}` (or
  `view/h`, `view/v`) as pgfplots projects it: the box's back edges and,
  with `grid=major`, the grid on its back faces; `surf` faces filled from
  the colormap by their height and painted farthest first, faceted or
  `shader=flat`; `mesh` lines coloured the same way; a curve's line and
  marks. Ticks and their labels stand along the bottom front edges and the
  leftmost upright one, `xlabel`, `ylabel` and `zlabel` beside them, and
  `colorbar` sets the colormap beside the box. Colormaps: pgfplots' `hot`,
  `hot2`, `jet`, `viridis`, `cool`, `blackwhite`, `bluered`, `greenyellow`,
  `redyellow` and `violet`, by `colormap/name` or `colormap name=`. A grid
  is read from a list or a table a row at a time while y holds, or as
  `mesh/cols` says.
- **Commutative diagrams are drawn**, each as the TikZ picture it is: a
  matrix of its objects and an arrow for each, its labels on the side the
  package puts them. tikz-cd's `\arrow` -- ``"f"``, ``"f"'`` swapped,
  `description`, `hook`, `tail`, `two heads`, `mapsto`, `dashed`,
  `Rightarrow` and `equal` as double lines, `bend left`, `shift left`,
  `phantom`, `from=`, spacing by tikz-cd's names; xy-pic's `\xymatrix`, with
  `^`, `_` and `|` labels, `@{-->}` shapes, `@<1ex>` shifts, `@/^/` curves
  and `@C=`/`@R=`; amscd's `CD`, its `@>>>`, `@VVV`, `@<<<`, `@AAA` and `@=`.
  xy-pic's objects are drawn as written: `*+[F]{A}` framed, `*++[o][F]{q_0}`
  round, `[F-]` dashed, `[F.]` dotted, `[F=]` doubled, `[F-:<3pt>]` its
  corners rounded, `+` and `+<2pt>` growing its margin, and `\txt{a\\b}`
  text in stacked lines; their modifiers used to be set as text.
  A display holding one alone is set as the picture, its number level with
  its middle, as tikz-cd sets a diagram on the formula's axis; one in a
  formula of other things is the grid of its objects.
- **Circuits**: quantikz's block and Qcircuit's `\Qcircuit` -- a wire a row,
  gates boxed on it, `\ctrl` joined to its `\targ`, open controls, swaps,
  meters, classical wires doubled, `\lstick` and `\rstick`.
- **Trees**: forest's `[S [NP] [VP]]`, and qtree's and tikz-qtree's
  `\Tree [.S the cat ]` with bare words as leaves, each parent centred over
  its children. Every label is set before the tree is placed, so a subtree
  stands as wide as its text is -- a formula, a `\Huge` word -- where its
  letters were counted and wide labels ran into each other.
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
- **LaTeX's own picture**: `\begin{picture}(w,h)(x,y)` is a canvas in
  `\unitlength`, and `\put`, `\multiput`, `\line`, `\vector`, `\circle`,
  `\circle*`, `\oval`, `\qbezier`, `\bezier`, `\makebox`, `\framebox` and
  `\dashbox` of a size, `\thicklines`, `\thinlines` and `\linethickness`
  draw on it (`core/picture.mtex`, over the TikZ paths). Outside a picture
  `\makebox` and `\framebox` are what they always are.
- TikZ's `overlay` and `remember picture`: the picture takes no room, and
  `current page` and `current page text area` are nodes, so a picture can
  stand at a page's corner.
- **EPS pictures are drawn**: `\includegraphics{chart.eps}` runs the file's
  PostScript and draws what it paints as a form, as a PDF figure is drawn,
  in the box its `%%BoundingBox` gives: paths, arcs, fills, strokes, clips,
  dashes, colour in grey, RGB, CMYK and HSB, transformations -- each path
  through the one it was built under -- procedures, dictionaries, loops,
  conditions and `stopped`; text in the standard face its font's name is
  nearest (Helvetica, Times, Courier, Symbol, each cut); an image whose
  samples follow it in the file, inline. A DOS EPS's binary header is
  passed over, a font's encrypted part skipped, and a program that never
  ends stopped where it stands.
- **SVG pictures are drawn**: `\includesvg{shape}` and
  `\includegraphics{shape.svg}` read the file's elements -- rectangles (rounded too), circles, ellipses, lines,
  polylines, polygons and paths,
  every command of `d` -- through each group's and each element's
  `transform`, filled and stroked as their attributes, the `<style>` rules
  naming their element, class or id, and their `style` say, with
  `opacity`; `<use>` draws again what it names, a gradient is its first
  colour, `clip-path` clips, and text is set in the standard face its
  family is nearest, its spans in their order and from its anchor. A
  pixel is three quarters of a point, as Inkscape exports one.
- `\includegraphics{figure.tikz}` (or `.pgf`) reads the file as the picture
  it writes; mwe's `example-image`, `-a`, `-b`, `-c` and their kin are framed
  boxes of their size with their name in them.

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
  `pdfsubject` and `pdfkeywords` `\hypersetup` gives; an `/ID` made from the
  file's own bytes, the same for the same document every time.
- **PDF/A-2b**, the archival standard, for pdfx's `a-2b` (and its kin) or
  `\DocumentMetadata{pdfstandard=A-2b}`: the file's information again as XMP,
  and an output intent with an sRGB ICC profile made by the engine; every
  face is embedded already, and every link marked to print.

### The command line

`latex [options] [document]` takes TeX's own options, with one dash or two
as TeX does, so a script written for pdflatex runs it unchanged:
`-h`/`--help`, `-v`/`--version`, `-o`/`--output-directory`, `-j`/`--jobname`,
`-i`/`--interaction` (batchmode prints nothing but errors), `-q`/`--quiet`,
`--halt-on-error` (no PDF from a document with an error), `--draftmode`
(every error found, no PDF written), `--file-line-error` (each error as
`file:line:column: message`), MiKTeX's `-I`/`--include-directory` (another
folder the document's inputs are read from, the engine's
`latex::Host::directories`) and `--time-statistics` (the engine's own
report); a value after `=` or as the next argument, and `--` ending the
options. `-t`/`--target` says how the document is made: `aot`, a PDF ahead
of time, the default; `jit` is taken and does nothing yet. `wasm` is no
longer one: WebAssembly is a build of the engine (see For programs), not
something a document is made into.
`--assets=DIR` names the engine's assets for a program installed away from
them. A run says what it wrote as TeX does -- `Output written on paper.pdf
(11 pages, 423495 bytes).` -- and a document named without an extension is
found as `.mtex`, then `.tex`. The exit status is 0 for a clean document, 1
for one with an error and 2 for a mistake on the command line. The logger's
`--debug`, `--trace`, `--log-level`, `--log-file` and `--no-color` are listed
with them. Values and commands a program hands a document are the bindings'
alone: the command line has no `--set`. `latex::compose` takes where its
report and its errors go, an empty destination for a draft, and where each
page's text goes.

- **The program is `mlatex`.** `latex` is the command every TeX
  distribution installs, and the two could stand on one PATH; the
  executable, its messages (`mlatex: no option ...`), `--help`,
  `--version` and every package now say `mlatex`, and the banner reads
  `This is mLaTeX 0.1.0.` The CMake target keeps the name `latex`, which
  an IDE's run configuration follows.
- **Names in words, with dashes between.** `--job-name`, `--draft-mode`
  (`-n`) and `--interaction=batch-mode`, `non-stop-mode`, `scroll-mode`,
  `error-stop-mode`; TeX's run-together `-jobname`, `-draftmode` and
  `batchmode` are still taken, so pdflatex's scripts run unchanged. Every
  option is one row of a table -- letter, name, TeX's spelling, value,
  heading, summary -- that both the parser and `--help` read, so adding one
  is a row and a branch, and the help can never disagree with the parser.
- **`-T`/`--time`** prints how long the run took: finding the document and
  its assets, setting it and writing its PDF, and in all, in milliseconds.
  `--time-statistics` still prints the engine's own report, step by step.
- **`-w`/`--watch`** sets the document again each time it is saved, until
  interrupted; **`-O`/`--open`** shows the PDF in the system's viewer once
  written, through `ShellExecuteW`, `open` or `xdg-open`, never a shell.
- **`--offline`** keeps a run off the network: `\httpget`, `\httppost` and
  `\includegraphics{https://...}` are refused where they stand, each named
  in an error (``\httpget of '...' refused: the run is offline``), and no
  request is made. `latex::Host::offline` is the same for a program, and
  the C library's `offline(session, on)` -- last in its struct, so a
  program built against the older header still finds every other
  operation where it was.
- The banner is `This is latex 0.1.0.`, without the target; the logger's
  `--log-level` and `--log-file` say they take their value after `=` rather
  than reading the next argument as the document.

### Building

- **Any profile builds on Windows.** cl has no C++26 and no `#embed`, and
  neither has a MinGW GCC before 15 -- CLion's bundled one is 13 -- nor can
  either link vcpkg's `x64-windows` libraries. A configure that names cl, a
  GCC or no compiler at all now takes the clang-cl of the newest Visual
  Studio that has one (found through vswhere, previews included) before
  `project()`, so CLion's own "Visual Studio" and "MinGW" profiles build
  as the presets do. A clang-cl build links its manifests with `llvm-rc`
  beside it, so no Visual Studio shell is needed.
- vcpkg's toolchain is found when no command line names one: VCPKG_ROOT's,
  or `C:/vcpkg`, whichever has libraries installed -- not the empty copy a
  Visual Studio shell points VCPKG_ROOT at.
- The presets name clang-cl by its full path and tell CLion to run them in
  its Visual Studio toolchain (`vendor` › `jetbrains.com/clion`).
- **A release build packages itself.** `cmake --install` lays out `bin/`
  (the program, the C library and on Windows every DLL it loads),
  `assets/` (fonts, hyphenation) beside it, `lib/` and `include/latex/`
  -- the shape `latex::locate` finds its assets in from anywhere. In a
  Release tree, `cpack` (or the `package` target) writes them all into the
  project's own `bin/` folder, beside the sources (was `release/`): an MSI
  with WiX 4 or later's `wix` (WiX 7
  once its EULA is accepted) or WiX 3's tools, and a .zip, on Windows; a
  disk image holding an `mLaTeX` folder to drag onto Applications, a .pkg
  installer that puts it in `/Applications/mLaTeX`, a .tar.gz and a .zip
  on macOS; a .deb, an .rpm when rpmbuild is there, a .tar.gz and a .zip
  on Linux, the first two under `/opt/mlatex` so TeX Live's own `latex` is
  left alone. The MSI was built with WiX 7 and run from its files. Each is named for
  its system and architecture -- `mlatex-0.1.0-windows-x64.msi`,
  `mlatex_0.1.0_arm64.deb`, `mlatex-0.1.0-macos-universal.dmg` when
  `CMAKE_OSX_ARCHITECTURES` names two -- and another architecture is
  another build.
- **One workflow builds every package.** `.github/workflows/build.yml`
  builds, tests and packages on six runners -- Windows, Linux and macOS,
  each on x64 and ARM64 -- with a C++26 compiler on each (Visual Studio's
  clang-cl, Clang 20, Homebrew's LLVM) and vcpkg's libraries cached
  between runs. The Windows runners install WiX 7 and accept its EULA, so
  each makes an MSI; every runner collects its packages from `bin/`,
  and a pushed `v*` tag attaches them all to a GitHub release.
  The Windows runners are the Visual Studio 2026 images (`windows-2025-vs2026`, `windows-11-vs2026-arm`): the 2022
  image's Clang
  19 and CMake 3.31 cannot ask for C++26 between them. vcpkg's toolchain is
  handed over as `TOOLCHAIN`, not `VCPKG_ROOT`, which loading Visual
  Studio's environment points at the empty vcpkg Visual Studio ships.
  Every action runs on Node.js 24 -- `checkout` 7, `cache` 6,
  `upload-artifact` 7, `download-artifact` 8, `action-gh-release` 3 --
  and Visual Studio's environment is loaded by running vcvarsall in the
  workflow itself, since the action that did it has no release that does.
  On macOS the libraries are built for 14.0, as the engine is, rather than
  for the runner's own 15: the difference drew a linker warning for every
  object of HarfBuzz and FreeType in every test, by the thousand.
- **The sample paper is set on GitHub.** The Windows x64 runner sets
  `build/main.mtex` with the program it just built and keeps the result as
  `main.pdf`, to open and see how the engine sets a real paper: from any
  run of the [Build workflow](https://github.com/aveloux/mlatex/actions/workflows/build.yml),
  under its artifacts, and once a version is tagged, at
  [releases/latest/download/main.pdf](https://github.com/aveloux/mlatex/releases/latest/download/main.pdf).
- **The package documents are with the tests.** The 523 sample documents,
  one per package and class, move from `build/packages/` to
  `tests/packages/`, beside the test that sets every one of them;
  `build/` keeps only the sample paper.
  It replaces a workflow that installed Skia, which the engine has not
  used for months, and compiled with a GCC that has no `#embed`.
  Dependabot keeps the workflow's actions current.

### For programs

- `latex::Session`: values, commands, packages and pictures handed in once
  and kept across documents; `set`, `define`, `provide`, their undoing verbs,
  `typeset`, `pdf`, `error`, and `pages()` for each page's text.
- The same interface as a C library (`protocols/ffi.hpp`) and as a WebAssembly
  module (`render/wasm.hpp`).
- **The engine is `namespace latex`**, in `latex.hpp` and `latex.cpp` (was
  `engine`, in `engine.hpp`): `latex::Session`, `latex::Host`,
  `latex::compose`, `latex::locate`. The command line is the CMake target
  `latex`, the program an IDE runs, and the engine's library the target
  `core`.
- **The C library's one function is `engine()`** (was `latex()`): with C
  linkage its name is global, and GCC and MSVC refuse a function named as
  the engine's namespace is. Its visibility macro is `VISIBLE`, undefined
  again at the header's end, and the library's build defines `EXPORTING`.
- **A snippet, as well as a document.** `latex::Session::snippet` sets a piece
  of LaTeX with no preamble -- `$...$`, `\[...\]`, an `align`, a table, a
  picture -- as a `standalone` document with amsmath and amssymb loaded, and
  hands back a PDF whose page is cropped to it, for an app or a web page to
  show among its own content. The piece's first line is the document's, so
  a mistake in it is reported on its own line. The C library has it as
  `snippet`, last in its struct so a program built against an older header
  finds everything else where it was, and the WebAssembly module as
  `session.snippet(text)`.
- **The WebAssembly module is built and released.** The workflow builds it
  with Emscripten 6.0.10 against vcpkg's `wasm32-emscripten` libraries, sets
  a snippet and a document with it in Node through its own JavaScript, and
  packs `latex.js`, `latex.wasm` and `latex.data` into
  `mlatex-<version>-wasm.zip`, which a tagged release carries beside the
  other packages.
- **An error names the file it is in.** Each line of `Session::error`, and
  of what `latex::compose` and `latex::typeset` write, starts with the file
  its position is in: `appendix.tex:3:5: error (macro): Undefined control
  sequence \foo`. A file a document inputs is named by the key it was found
  under, so `\input{appendix}` reports `appendix.tex` when that is the file
  provided. The document handed to `typeset()` is named by the file provided
  with the same text, when one was -- an editor that provides every file of
  its project and sets one of them reads that file's name back -- and
  `main.tex` when none was, a snippet included; a document read from disk by
  its own file name, without its folder. `memory::Location` carries the file
  as a number, `syntax::Mouth::files` holds the names and
  `syntax::Traceback::format` takes them; a position in text that is no
  file's, what a primitive composes, is still a line and a column alone, and
  those are the only lines `--file-line-error` still adds a name to. The
  Session's methods, and the C and WebAssembly ones, are the same.

### Speed

Release build, clang-cl, one run each, median of thirty:

| Document                                                                                          | Pages | Time    |
|---------------------------------------------------------------------------------------------------|-------|---------|
| a one-line article                                                                                | 1     | 3.2 ms  |
| a Russian article with babel, its contents and a bibliography, two passes                         | 1     | 4.6 ms  |
| two tikz-cd diagrams, an xy-pic and an amscd one, a matrix, an automaton, a circuit and two trees | 1     | 4.7 ms  |
| an Arabic article with polyglossia, right to left                                                 | 1     | 5.8 ms  |
| the paper in `build/main.mtex`                                                                    | 8     | 11.9 ms |

A position that says its file makes a token 40 bytes where it was 32,
which costs the paper in `build/main.mtex` about 2%: 30.65 ms against
29.90 ms, the median of forty runs each of the same tree with and without
it, as `--time` reports setting it and writing its PDF.

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
  against it; a release engine now tells everything linking it (`LATEX_RELEASE`).
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
- A line could end at a penalty of ten thousand -- `\nobreak`, a tie -- when
  every other break cost more; as in TeX, none ever does now.
- A binary operator with nothing after it before a formula's closing `$` or
  a group's `}` -- `$a+$`, `{x-}` -- took the closing token as its operand
  and read on past the formula.
- **A long table ran off the page.** longtable, `longtable*`, supertabular,
  xtabular, tabularray's `longtblr`, xltabular and tabu's `longtabu` were one
  box: a table taller than a page jumped to the next and ran past its foot,
  the rows below lost. They are set a row at a time now, so a page ends
  between two rows; the rows before `\endhead` stand atop every page the
  table carries on to, those before `\endfoot` close every page it breaks
  off, and `\endfirsthead` and `\endlastfoot` give the first page and the
  last their own (the page builder's `Repeat`). The table is centred, or
  set to the side `[l]` or `[r]` names, `\LTpre` above and `\LTpost` below;
  its `\caption` is a table's, across every column in a box of no width, so
  it widens none of them -- it was a figure's, and widened the first column.
- A formula in a table's paragraph cell, a minipage or a `\parbox` was one
  box and ran out of its cell over the next; it ends a line after a relation
  or an operator, as it does in running text.
- `\url`, `\path` and `\nolinkurl` turn over after a slash, a stop and the
  rest of url.sty's characters (`\@breakable`), instead of running out of
  a narrow column. A typewriter face breaks no word with a hyphen, as TeX's
  `\hyphenchar` of -1 has it, and a word is broken only in its first run of
  letters, as TeX's is: `a/very/long/path` was broken as `long/-path`.
- `\multirow` stands in the middle of the rows it spans -- level with the
  first for `[t]`, the last for `[b]` -- not at the top of the first; tabu's
  `X[l]`, `X[c]` and `X[r]` were read as three more columns each; makecell's
  `[l]`, `[r]`, `[t]` and `[b]` were taken for a vertical position.
- A BibTeX entry missing the first part of a block no longer starts it with
  the second's comma: a `@misc` with no `howpublished` was `Notes. , 1843.`
  A book's edition is in lower case after its publisher, as plain sets it:
  `Addison-Wesley, second edition, 1994`.
- On Windows the JPEG and WebP libraries are copied beside the program and
  the C library, debug or release as the build is. Being delay-loaded hid
  them from vcpkg's own copy, and the first WebP picture a document drew
  ended the program with 0xC06D007E unless the DLL happened to be on PATH.
- **`test_main` read the command line's exit status wrong on Linux and
  macOS.** POSIX's `system()` returns a wait status, not the exit code --
  `WEXITSTATUS` is what reads the second out of the first. Every check for
  a mistake's exit status of 1 or the command line's own of 2 compared
  against `1 << 8` and `2 << 8` instead and failed, which is what the
  workflow's first run on every runner actually found.
- **The engine builds on macOS again.** Apple's libc++ marks
  `std::from_chars` for `float` and `double` unavailable before macOS 26,
  at compile time, so every one of the 58 places that read a number that
  way stopped the build. `patch::decimal` (`patch.hpp`) reads the same
  grammar through `strtof` and `strtod`, behind the same signature and
  result; reading a whole number with `std::from_chars` was never affected
  and is unchanged. `std::abs` on a float is now included from `<cmath>`
  wherever it is used, rather than reached through whichever header a
  standard library happened to pull it in with.
- Three pipes in the workflow's PowerShell had been written `\vert{}`, which
  PowerShell cannot parse, and the Windows runners failed before building.
- The WebAssembly module's bindings were behind `#if defined(__WIN32)`, which
  Emscripten never defines, since a stray edit in `a323f9a`: the module built,
  and handed JavaScript nothing. They are behind `__EMSCRIPTEN__` again, and
  the workflow now calls them from Node, so it cannot happen silently.
- `\evaluate{1 ^ 2147483647}` multiplied its way through all two billion
  powers. A base of 0, 1 or -1 is answered at once; any other passes a
  register's limit within 31 steps, where the loop already stopped.
- A `\numexpr` product past 64 bits --
  `2147483647*2147483647*2147483647*2147483647` -- wrapped round, which C++
  leaves undefined, before the clamp to a register's range ever saw it.
  Every value along the way is now held within 2^53, where a double is still
  exact, so the product stops at the limit; `100000*100000/100000` still
  keeps its product past 32 bits until it is divided.
- `\foreach \x in {0,0.1,...,1}` made its items by adding the step again and
  again, so the rounding of each addition piled up; each item is now the
  first plus a whole number of steps. So are a TikZ grid's lines, a
  pgfplots axis's ticks and its `xtick={0,0.1,...,1}`, and a function's
  samples, each counted with an integer; the tick step's thresholds are the
  square roots of 2, 10 and 50 they were written as approximations of.
- A program that loaded `latex_ffi.dll` from a folder of its own, as Python's
  ctypes does from a wheel, ended at its first JPEG or WebP picture: Windows
  looks for a delay-loaded library beside the program rather than beside the
  library that asked for it. The library's delay-load hook now loads the
  JPEG and WebP libraries from its own folder first.

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
- Then: `test_strings` and `test_records`; numbers in words; a font's wide
  variants; `\the\value`; category changes in a row; and in `test_latex`
  every one of the 523 documents in `tests/packages`. 85 tests pass in Debug
  and Release, with no warning in either build.
- `build/main.mtex` has a third appendix, **Kernel checks**, setting what
  LaTeX's own kernel gives a document with no package at all: every face,
  series, shape and size; accented and foreign letters and the specials;
  every space and fill; the text blocks, tabbing and `list`; lists four deep
  in every kind; notes, counters and lengths; `\def`, `\csname` and the `\if`
  family; every box; a tabular with every rule; LaTeX's picture; maths --
  Greek, alphabets, accents, relations, arrows, dots, matrices, braces,
  radicals, delimiters, operators and limits; and every kind of reference.
  The paper runs to eleven pages, and the test reads the appendix's text.
- Then: a long table over pages, its heads and feet, every row kept, its
  caption; `\multirow`, tabu's X columns; the pager's repeated head and foot;
  an address turning over unhyphenated; a word's first run of letters.
- Then: `test_patch` -- a number read to its end, stopped at a unit, a
  failure that leaves the value alone, and a range read to its own end and
  not its text's. 86 tests pass in Release.
- Then: `\evaluate`'s powers of one, minus one and zero; `\numexpr`'s
  products past 64 bits, either way, and past 32 until divided; `\foreach`'s
  ranges in tenths, counted down, and in letters.
- Then: an error in a provided file a document inputs, reported under that
  file's name at its own line and column, and one in the document under
  `main.tex` or the name it was provided under (`test_latex`); a traceback
  with and without the names (`test_traceback`); a file's number on its
  tokens, kept when it is read again (`test_mouth`). 86 tests pass in Debug
  and Release, with no warning in either build.

### Documentation

- `src/modules/packages.md` (was `docs/packages.md`): where a package is found (aliases, provided packages,
  warnings), the glossary, classes (beamer, letter, `\@bodyshape`,
  `\@bibkind`), long definitions, characters and category changes, links and
  text given later, and environments as native blocks. The Doxygen reference
  builds with no warnings.
- Then: ifthen's test language, `\@ifempty` as the TeX test it is, a body
  that must end in the `\@ifnextchar` deciding it, xstring and the data
  tables, environments from the glossary, what a category change in a
  package's own file reaches, and a package's sample document in
  `tests/packages`.
- **The reference reads better.** Doxygen's own look, unstyled, with a
  light and dark theme switched from the top bar and the project's name and
  version at the top. Every block of code has a copy button, sections
  fold, and long pages list their headings. The README is the
  front page, and the security policy, this changelog and the code of
  conduct are pages beside the package guide; headings take GitHub's
  anchors, so a README link to one of its sections works in both places.
  Four code spans here held a backtick or a quote that Doxygen paired
  wrongly, which turned the rest of the file inside out; they are written
  with double backticks, and the reference still builds with no warnings.
- The root `Doxyfile`, which named a folder that is gone and none of the
  settings the `docs` target uses, is removed: the target is configured in
  `CMakeLists.txt` alone.

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
  (the ledger's); what `compose` starts `dispose` ends, the logger included (`Logger::dispose`, which was `close`); the
  expander's `forget` takes back
  what `define` gave, as the language's `\forget` does (`undefine`).
- `CatCodes` is `Catcodes`, and the last snake_case names (in the plots
  module) are single words.
- **The font collection is `typography::Collection`** (was `Library`, in
  `collection.hpp`), on the engine's own verbs: `post(directory)` indexes a
  tree and `post()` the system's folders (were `survey` and `system`),
  `set(name, family)` points a name at a face (was `alias`), `get(family)`
  reads one (was `read`); how many faces it holds and how many bytes are
  resident are its fields `faces` and `bytes`, as the registry's `fonts`
  and `faces` and the document's `blocks` are (were `count()`, `resident()`,
  `opened()`). The step that turns a parsed child into its boxes is
  `render::primitives::compose` (was `gather`), and the lambdas named
  `gather` are named for what they do.
- `#embed` needs no pragmas: Clang's claim that it is an extension under
  C++26 is turned off once in CMakeLists.txt, and the
  `#if defined(__clang__)` blocks around the three files that embed are gone.
- `CMakePresets.json` has `debug` and `release` presets on clang-cl, the
  debug one exporting compile commands, so an IDE -- CLion with its Visual
  Studio toolchain -- reads the code as C++26 rather than through cl, which
  cannot configure the project and leaves every file unresolved.
- **`namespace protocols`** (was `network`, in `include/network/` and
  `src/network/`, now `include/protocols/` and `src/protocols/`): the HTTP
  client `\httpget`, `\httppost` and `\includegraphics` share, and the C
  library beside it. A module named for a layer of the internet read as
  though the engine had one of its own; `protocols` is what it actually is.
- **No field is a single letter.** A colour's channels are `red`, `green` and
  `blue` (were `r`, `g`, `b`), in the graphics colour and the layout's alike;
  a canvas point is `across` and `up`, a point in space `across`, `along` and
  `up`, and a place on the page `across` and `down` (were `x`, `y`, `z`). A
  glyph's displacement from the pen is its `offset` and `shift`, as a box's
  is, a glyph's ink starts at its `bearing` and `top`, and a tree node stands
  at its `center`. A transform is where a unit step right and a unit step
  down land, `horizontal` and `vertical`, and its `offset` (were `a`, `b`,
  `c`, `d`, `x`), as a projection is where its three axes land (`across`,
  `along`, `up`); the PostScript reader keeps its `current` point and the
  `start` of the subpath as positions of their own. A curve's points are
  weighed once per step, its four Bernstein `weights`, rather than in one
  long sum.
- **Text a primitive writes is a format.** What the syntax primitives build
  to be read back -- a block's `\name:open`, a CSV row's
  `\@shared\@define\thecsvrow{1}`, a `\begin{tabular}{...}`, an
  `\ifnum ...\relax 1\else 0\fi` test -- and the paths and keys they look
  up are `std::format` strings, not chains of `+` making a temporary at
  every step; a decimal's fraction is a zero-filled width, `{:0{}}`. They
  are clean under the clang-tidy checks CLion runs: raw string literals,
  `std::cmp_less` where a signed number meets an unsigned size, explicit
  conversions where an integer becomes a `long double`, every field of an
  evaluation's state initialised, designated initialisers for a token and a
  register's target, and no local hiding another (`\csname`'s name is
  `built`, `\DTLsetseparator`'s `given`, the register reader `quantity`).
  A value read only for its test is an if-initialiser, as CLion has it,
  the 128-bit product's halves are `alpha`, `beta`, `theta` and `gamma`,
  `\DTLsort`'s comparison is a named `before`, and the date's registers
  take `std::tm`'s fields with no cast.
- **And so does the render layer's.** Every BibTeX house style writes its
  entry as a template -- ACM's `{}. In \emph{{{}}}{}. {}{}.` -- and so do
  TikZ's paths and messages, beamer's overlays, longtable's caption macro,
  a theorem's head, the index's page ranges and every `Unknown ...` report.
  The accents' sixteen combining marks are a table where they were a
  ternary sixteen deep, three empty branches of TikZ options it lets go are
  one list, `ignored`, and the quantum circuit's `r` and `c` are its `row`
  and `column`. clang-tidy's fixes went in where they were right --
  `contains` for a membership test, a character where a one-letter string
  was, raw strings for TeX, `std::cmp_less` and its kin -- and one was
  turned down: it rewrote `listed != static_cast<std::size_t>(-1)` as a
  comparison with -1 that always holds, so the sentinel is a named
  `unlisted`. A selection assigned to itself no longer counts as a change, a
  footnote's number is read with `std::from_chars` rather than `atoi`, a
  label's text is a `std::string_view` of the split it came from rather
  than one built from iterators, the values a message is built from are
  taken by reference, and every local that hid another has a name of its
  own (`stroke`, `breadth`, `settings`, `beside`, `entered`, `ruling`,
  `styling`, `indentation`, `abbreviation`, `joined`, `separator`).
- **A handler that only reads its parser or its expander takes it `const`.**
  A primitive's lambda that reaches the arena and the expander through
  the parser's references, or reads the expander without moving it on --
  `\bibitem`, `\bibliography`, `\printbibliography` and their like -- takes
  `const syntax::Parser &` or `const syntax::Mouth &`, as CLion asks; each
  was tried by compiling, and kept only where nothing it does needs more.
- **Every struct built in the primitives names its fields.** The tables --
  the 118 accented letters, the 102 rules of a formula's operators, the
  document classes, the named spaces, the displays, the font switches --
  and every point, colour, slice and entry made on the way are designated
  initialisers, `{.accent = '\'', .base = 'a', .result = "á"}`, so a field
  moved in its struct can never be filled with its neighbour's value. A
  table's rows are packed as many to a line as fit, and a row too wide for
  one line breaks before the same field in every row of its table.
  The numbers a colour is defined by are `first` to `fourth` -- red, green
  and blue in one model, cyan, magenta, yellow and black in another.
- **A struct one function uses is that function's own.** The accented
  letters, xcolor's named colours, libjpeg's error path, the compiled
  hyphenation trie, the WinHTTP handle and the command line's options were
  types of their files, which clang-tidy's `misc-use-internal-linkage`
  would put in an anonymous namespace; each is declared in the one function
  that uses it instead, where a type has no linkage to give. The trie is a
  plain struct sized by the constants measured just before it, where it
  was a template, and libjpeg's callback is a lambda.
- **The maths symbol table is read without an instance.**
  `syntax::expression::Unicodes::get` is static, and so is the formula
  parser's `delimiter`, which only reads it; the parser no longer takes or
  holds a `Unicodes` it never used, and the brackets it knows are
  designated initialisers.
- **The primitives are grouped by what they do.** Each layer's primitives
  folder keeps its wrapper and context and holds a folder per cluster, each
  folder its own namespace and its tests beside it. The syntax primitives
  are in definitions (macros, variables, hooks, packages), control
  (conditionals, loops, environments), calculations (arithmetic, decimals,
  registers) and data (strings, records); the render primitives in text
  (styles, typefaces, colors, characters, languages, verbatim), spacing
  (boxes, glue, groups, paragraphs, penalties, rules), structure (pages,
  sections, lists, floats, footnotes, theorems, tables, algorithms),
  referencing (references, citations, counters, numerals), notation
  (equations), pictures (illustrations, plots, diagrams) and network
  (requests). Modules whose names said less than they do are renamed with
  their classes, wrapper members and tests: relay is conditionals, blocks
  environments, compute arithmetic, values registers, include packages,
  expressions equations, symbols characters, spacing glue, page pages,
  typeface typefaces and numeral numerals, so the core wrapper's relay is
  its conditionals and a render context's blocks its environments. No
  cluster takes a name the render layer already has -- layout, graphics,
  typography -- so a layout node is still `layout::Node` inside every
  module, and a source names the classes it takes from other clusters once,
  after its namespace opens, as `using text::Styles;`.
- **The clusters are named for what they stand for**, the way definitions
  is, not for what kind of thing they handle. The syntax primitives are in
  definitions, logic (conditionals, loops, environments), arithmetic
  (integers, decimals, registers) and bookkeeping (strings, records); the
  render primitives in typesetting (styles, typefaces, colors, characters,
  languages, verbatim), arrangement (boxes, glue, groups, paragraphs,
  penalties, rules), composition (pages, sections, lists, floats, footnotes,
  theorems, tables, algorithms), referencing, mathematics (equations),
  figures (illustrations, plots, diagrams) and retrieval (requests). The
  arithmetic module is integers, beside decimals. Folders, namespaces and
  tests move together; the render layer's own layout, graphics and
  typography keep their names, so `layout::Node` inside a module is still
  the engine's node.
- The table of contents reads each entry as a structured binding, and the
  boxes its number is set in are its `numeral`, apart from the entry's
  `number`; the counters' installer takes its parser `const`, a footnote's
  and an equation label's context too.

### Removed

- **Code nothing runs**, found by relinking the engine and the C library
  with every function a section of its own and asking the linker what it
  threw away, then by clang-tidy and a build with Clang's unreachable-code,
  unused-macro and unused-member checks on: `ligate()`, which nothing called;
  `Cursor::consumed()` and the count of tokens it read, kept up on every
  token for a test alone; `Glossary::size()`; 23 `#include`s nothing in
  their file used, and one included twice. `Glossary::define` looks its line
  up through `Glossary::get`, as a reader of the class expects.
- **The sandbox** (`memory/sandbox`: `VM`, `Allocator`, `Policy`), which
  only its own tests ran. `VM` drove the language alone, with no page
  behind it, and none of `Policy`'s rules reached the engine that
  `latex::compose` runs; what keeps a document in bounds is that engine's
  own: no shell, no file written, reads confined to its folders (see
  SECURITY.md).
- The old `render/primitives/structure`, `configuration` and `expression`
  folders, replaced by one flat module per concern; fontconfig lookup, in
  favour of the font library over `assets/fonts`; margin protrusion; the
  syntax cache; checked-in PDFs and `build/main.tex`.

### Not yet

By choice, never: `\write18`, reading a `.sty` or `.cls` from disk, and a
second run's `.aux` -- the engine's one pass does what that run is for. A
beamer sidebar theme's sidebar, text inside an EPS or SVG picture in its
own embedded font (it is set in the nearest of PDF's standard faces), and
a gradient's shading (it is its first colour) are drawn near enough, not
exactly. Coverage figures above count packages that load and whose common
commands set text — not that every feature of each is drawn.
