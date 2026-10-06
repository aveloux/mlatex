# Writing packages {#packages}

A package here is a folder with one file in it, `main.mtex`, written in the
same language a document is. There are no `.sty` files and no class files:
what makes LaTeX's packages slow -- thousands of lines of TeX macros
re-implementing the page, category-code games, several passes over the same
text -- is done by the engine's own primitives, in C++, once. A package is
what is left: the names a document writes, defined in terms of those
primitives. Most are a few dozen lines.

This page is for writing one. It covers where a package is found, the
language it is written in, how it reads its options, and what it should and
should not do.

## Where a package is found

`\usepackage{name}` looks for `name/main.mtex` in three places, in order:

1. **Handed in by the program** running the engine -- `latex::Host::files`,
   or `latex::Session::provide("name/main.mtex", text)`. A program can ship
   its own house style without writing a file anywhere.
2. **Compiled into the engine** -- every folder under `src/modules/`. These are
   the packages that come with it: `amsthm`, `geometry`, `siunitx` and the
   rest. `src/modules/main.mtex` lists them.
3. **Beside the document on disk** -- a folder `name/` next to the `.mtex`
   file -- or in a folder of packages the command line names with
   `-I folder` (`latex::Host::directories` for a program). This is the one to
   use while writing a package: no rebuild, no program, just a folder.

A package is read once however often it is asked for, and it is marked loaded
before it is read, so two packages that ask for each other do not loop.

When none of the three has it:

4. **Another package under its name** -- `xurl` is `url`, `soulutf8` is
   `soul`, `circuitikz` is `tikz`. `src/modules/core/compatibility.mtex` lists
   each as a variable, `\@setvariable{alias.xurl}{url}`, and asking for the one
   loads the other and marks both loaded.
5. **Nothing to read** -- a package whose whole job the engine does itself:
   the font packages, the drivers, LaTeX's internals, the page-layout tuners.
   The same file marks them loaded with `\@providepackage{...}`, grouped by
   why there is nothing to read.
6. **Not known at all** -- the document is still set. The package is marked
   loaded, a warning names it, and its commands are read from the glossary
   (below) when the document uses them. A command neither knows is an error
   where it is used, as in LaTeX.

## The glossary

CTAN has thousands of packages; most documents use a handful of commands
from a few of them. `assets/glossary.mtex` holds those commands, one line
each, as `\@define` takes them:

```
\contour[3][]{#3}
\microtypesetup[1]{}
\xrightarrow[2][]{\underset{#1}{\overset{#2}{\longrightarrow}}}
```

Nothing in it is read until a document uses a name nothing else gives a
meaning. The expander then asks `syntax::Glossary`, which the compiler built
from the file with `#embed` into a table by name, and the line is read as
`\@shared\@spanning\@define` followed by the line: defined for the rest of
the run, long as `\newcommand`'s commands are, and read again in place. A
document that uses none of it pays nothing; one that does pays one lookup
per name.

A line either keeps what its command is for -- `\contour{white}{word}` is
the word -- or lets it go, when the command only tunes what the engine
decides itself. A body that reads further arguments from the document must
end in the one command that does the reading: `\@gobbleoption\@gobble` reads
the `\@gobble`, not the document. The glossary's own `\@gobbleoptionone`,
`\@gobbleoptiontwo` and `\@gobblefont` are written for that.

A package that gives a name the glossary also holds wins wherever it is
loaded; the glossary is only the fallback. An environment is two lines,
`\name` and `\endname`: `\begin{name}` nothing else defines reads both from
the glossary, as LaTeX's `\begin` runs the two macros. When a name is on two
lines, the first wins.

## Classes

`\documentclass{name}` sets the page as LaTeX's article does, at the size
and on the sheet the options ask for, in the columns the class prints --
two for `IEEEtran`, `proc`, `aa`, `mnras`, and for `acmart`'s `sigconf`,
revtex's `reprint` and elsarticle's `5p`. A class with a folder of its own
under `src/modules/` is then read as a package is, its options as its
variables: `IEEEtran`, `acmart`, `llncs`, `elsarticle`, `revtex4-2` (and
`revtex4-1`, `revtex4`), `amsart`, `svjour3`, KOMA-Script's `scrartcl`,
`scrreprt` and `scrbook`, `beamer` and `letter`. Each sets its margins with
the page's own `\@leftmargin`, `\@rightmargin`, `\@topmargin` and
`\@bottommargin`, and its title page with a `\maketitle` of its own. A class
the engine does not know is set as article, with a warning.

A class's blocks are native like any other -- beamer's `frame`, `block` and
`columns`, the letter class's `letter` -- and what they look like is the
class's macros: beamer's `\frametitle` and `\titlepage`, the letter class's
`\@letter`, `\opening` and `\closing`. A class sets how the body is aligned
with `\@bodyshape` (beamer's is `\raggedright`), what its references are
headed by with `\@bibkind` and `\@bibtitle` (a report's are a chapter,
`Bibliography`), and its normal face's family with
`\renewcommand{\familydefault}{\sfdefault}`.

## Languages

babel and polyglossia are two packages over one native primitive, which
does what choosing a language does:

```
\@language{english,russian}   % the last named, or main=, is chosen whole
\@hyphenation{russian}        % its patterns alone, for a phrase
\@direction{RTL}              % paragraphs from here read right to left
\@languagecommands{arabic}    % \textarabic{...} and \begin{Arabic}
```

Choosing a language breaks its words by TeX's `hyph-*.pat.txt` for it, no
nearer a word's ends than it allows; sets its paragraphs right to left when
it is written so; sets `\languagename`; and reads its captions and date from
`babel/<language>.mtex`, the first time, then runs them:

```
\@define\captionsrussian{%
\@define\abstractname{Аннотация}%
...}
\@define\daterussian{\@define\today{...}}
```

Adding a language is a file of that shape under `src/modules/babel/`, and a
line in the table in `src/render/primitives/languages.cpp` giving each name
it goes by, its captions file, its patterns and their minima, and its
direction. A language with no captions file keeps the captions in force; one
with no patterns breaks no word.

A script the body's face has no glyphs for is drawn by the same design's face
for it (New Computer Modern) where the engine carries one, and by one of the
system's faces where it does not -- Chinese, Japanese. Arabic, Persian and
Urdu are drawn by the Noto Naskh Arabic the engine carries. Nothing in a
package chooses that; `\newfontfamily\arabicfont{...}` is read and let go.

## Diagrams

tikz-cd's `tikzcd`, xy-pic's `\xymatrix`, amscd's `CD`, quantikz's and
Qcircuit's circuits and forest's and qtree's trees are native: each is read
whole and written out as the TikZ it draws -- a `\matrix` of its objects, a
`\draw` for each arrow or wire -- which the TikZ primitives then draw. Their
packages only load TikZ, or are marked loaded. A package that draws in the
same way can do the same: write TikZ, with `\matrix` for a grid of objects
named by row and column, and `(a) -- node {label} (b)` for a labelled line.

## A first package

```
% letterhead: a company's name and address, set at the top of a letter.
%
%   \usepackage[compact]{letterhead}
%   \company{Acme Ltd.}\address{1 High Street}
%   \begin{document}\letterhead ...

\@define\@company{}
\@define\@address{}
\@define\company[1]{\@define\@company{#1}}
\@define\address[1]{\@define\@address{#1}}

\@define\letterhead{%
  \begin{flushright}
    {\Large\bfseries\@company}\\
    \@address
  \end{flushright}
  \@ifvariable{letterhead.compact}{\vskip 1em}{\vskip 3em}}
```

Save that as `letterhead/main.mtex` beside a document, write
`\usepackage{letterhead}` in its preamble, and it works. Everything in it is
covered below.

## The language

A package is written in the engine's own language, which is TeX's, with
plainer names beside TeX's own. Both spellings work; a package usually uses
the `\@` forms, which a document cannot redefine by accident.

### Defining

```
\@define\greet[2][Hello]{#1, #2!}     % two parameters, the first optional
\@define\pair#1,#2;{#1 then #2}       % delimited parameters, as \def's
\@alias\salute\greet                  % \let: a copy of what it means now
\@forget\salute                       % undefined again
```

`\newcommand`, `\renewcommand`, `\providecommand`, `\def`, `\edef`, `\let`,
`\NewDocumentCommand` and `\DeclareRobustCommand` all exist too, as LaTeX's
kernel has them, and a package may use them. A definition made inside a
group ends with it unless it is `\global` (or `\shared`). LaTeX's own
definitions take a paragraph in an argument unless starred --
`\newcommand*` -- and `\def` and `\@define` do not unless `\long` (or
`\spanning`) comes first. `\g@addto@macro\list{more}` adds to a macro, for
good, as LaTeX's kernel does.

### Names

- `\@name` -- a package's own. The engine's core uses `\@` names for its
  internals, so prefix yours with the package's name: `\@letterhead@rule`.
- `\name:part` -- reserved. A colon ends a control word, so no document can
  write one; the engine uses them for markers it injects itself.
- A name LaTeX's package of the same name defines should mean what it means
  there. A package named after a LaTeX package owns those names; it should
  not define names that belong to another.

### Options

Every option a document gives becomes a variable named for the package:

```
\usepackage[compact,margin=2cm]{letterhead}
```

sets `letterhead.compact` to `true` and `letterhead.margin` to `2cm`. Read
them with `\@variable{letterhead.margin}` and test them with
`\@ifvariable{letterhead.compact}{yes}{no}`. The list as written is kept too,
as `letterhead.options`, for a package that reads its options in order --
babel's last language is the one the document is in. `\@setkeys{family}{key=value,...}`
sets variables the same way from a command's own key list, which is how
`\geometry{...}` and `\sisetup{...}` work.

### Numbers and lengths

TeX's registers, with their names:

```
\newcount\@letterhead@lines   \@letterhead@lines=3
\advance\@letterhead@lines by 2
\newdimen\@letterhead@gap     \@letterhead@gap=1.5em
\newskip\@letterhead@skip     \@letterhead@skip=6pt plus 2pt minus 1pt
\the\numexpr 3*(4+5)/2\relax  % 14, rounded as e-TeX rounds
\the\dimexpr\textwidth-2cm\relax
```

`\set`, `\increase`, `\scale` and `\reduce` are the plain names for
assigning, `\advance`, `\multiply` and `\divide`. `\setlength` and
`\addtolength` take the calc package's sums: `\setlength{\x}{\textwidth-2cm}`.
`\calculate{3 * 12.50}` works in exact decimals, for money.

### Deciding

The whole `\if` family: `\ifnum`, `\ifdim`, `\ifx`, `\ifcase`, `\ifodd`,
`\ifdefined`, `\ifcsname`, `\ifmmode`, `\ifvmode`, `\iftrue` and `\iffalse`,
each with `\else` and `\fi`, and `\unless` in front of any.
`\@iftest{test}{yes}{no}` (or `\iftest`) reads a test as ifthen writes one --
`\value{page} > 1 \AND \NOT \equal{\x}{a}` -- which is how ifthen's and
xifthen's `\ifthenelse` are made. `\@ifstar`,
`\@ifnextchar` and `\@ifundefined` are there as LaTeX's kernel has them, and
etoolbox's and ifthen's tests come with those packages. `\@ifempty` is a TeX
test, not a choice between two groups: `\@ifempty{#1} none\else #1\fi`.

A body that `\@ifnextchar` (or `\@ifstar`) decides in must end in it: what it
looks at is the document's next token only when nothing of the body is left
after it.

### Text and data

xstring's tests and functions (`\IfSubStr`, `\StrBefore`, `\StrLen`,
`\StrSubstitute`, …) and csvsimple's and datatool's tables (`\csvreader`,
`\csvautotabular`, `\DTLnewdb`, `\DTLloaddb`, `\DTLforeach`, …) are native
commands under their packages' names, each worked out in one pass on the
expanded text. A package that takes text apart or reads rows can use them
rather than walking characters with macros.

### Repeating

`\repeat[3]{text}` reads its text so many times; plain TeX's `\loop ...
\repeat` is there as the kernel defines it.

### Hooks

`\AtBeginDocument{...}` and `\AtEndDocument{...}` run text where the document
opens and closes. `\@addtohook{name}{text}` and `\@usehook{name}` keep and run
hooks of a package's own.

### Other packages

`\@requirepackage{name}` loads another package first, as `\RequirePackage`
does. `\@providepackage{a,b}` marks packages loaded without reading anything --
for names whose whole job the engine already does, so a preamble asking for
them reads as written.

### Characters

`\catcode` works as TeX's does: a change reaches every character the
document has not been read up to yet -- ``\catcode`\!=13`` makes each `!` to
come active, ``\catcode`\|=0`` makes `|textbf` a command -- and a change made
in a group ends with it. Text a macro has already made keeps its categories.

### Links and text given later

`\@link{address}{text}` is text that goes to an address, and
`\@linkto{label}{text}` text that goes to where a label stands: links once
hyperref is loaded, framed or coloured as its options say, and the text
alone without it. `\@forward{key}` is text written in when `\@fulfil{key}{text}`
gives it, before or after -- how an acronym used before its list defines it
is spelled out.

## What a package should not do

- **Define environments.** `\begin{...}` and `\end{...}` are the engine's own,
  written in C++: every block a document uses -- `itemize`, `figure`,
  `tabular`, `multicols`, `tcolorbox` -- is native. A package gives commands.
  A document's own `\newenvironment` is native too: its begin and end code
  run inside the block's group, as LaTeX's do. A `\begin{name}` nothing
  defines runs `\name` and `\endname` when they exist, as LaTeX's `\begin`
  does -- `\begin{small}` -- and is otherwise a warning and a plain group.
- **Re-implement the page.** Headings, floats, footnotes, columns, lists,
  tables and the page's furniture are primitives. A package that needs one to
  look different should use the lengths, counters and names they read --
  `\parskip`, `\secnumdepth`, `\figurename`, fancyhdr's `\fancyhead` -- rather
  than rebuild them from boxes.
- **Play with category codes.** A package's names may hold `@` as LaTeX's
  do; a document's `\makeatletter` names keep theirs whole too. A `\catcode`
  change a package makes reaches the document, not the rest of the package's
  own file, which was read whole before it ran: a package that needs names a
  document cannot write -- expl3's `\c_true_bool` -- defines them through
  `\csname`, as `core/expl3.mtex` does, and gives a category by number,
  ``\catcode 58=11``, rather than as ``\catcode`\:``, which reads `\:` as a
  command.

## Handing a package in from a program

A program embedding the engine can give a document packages it has written
itself, and commands the document calls back into:

```cpp
latex::Session session(assets);
session.provide("letterhead/main.mtex", read("house-style.mtex"));
session.define({.name = "balance", .arity = 1, .handler = [](auto arguments) {
    return lookup(arguments[0]);          // text the document reads in its place
}});
session.typeset("\\documentclass{letter}\\usepackage{letterhead} ...");
```

## Testing a package

`tests/syntax/test_modules.cpp` checks every package that comes with the
engine: each loads, known by name, with no error reported, and what it
defines sets the text it should. It carries its own two harnesses: `sets()`,
which runs a document through the expander and the parser and compares the
text, and `typeset()`, which runs the whole engine and reads the PDF's text
back. A new package should add its name to the list there and its checks
beside the others, and a line to the index in `src/modules/main.mtex`. A
class's module is checked the same way from `tests/render/primitives/test_page.cpp`,
and the glossary's lines from `tests/syntax/test_glossary.cpp`.

`build/packages/` holds one small document for each package and class the
engine reads -- 509 packages and 14 classes -- each using what its package is
loaded for. `tests/test_engine.cpp` sets every one and fails on any error or
any package or command the engine does not know, so a new package should come
with its document there: `build/packages/<name>.mtex`, a whole document from
`\documentclass` to `\end{document}`.
