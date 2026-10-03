# Swedish → English translator

A Mudlet package for learning Swedish while you play: translate text on demand or
automatically, and choose how the translation is shown from six visualizations.

## Install

In Mudlet, open **Toolbox → Package Manager → Install** and pick `SwedishTranslator.mpackage`,
or run `lua installPackage("/path/to/SwedishTranslator.mpackage")`. Then type `sv demo`.

## Use

| Command | What it does |
| --- | --- |
| `sv <swedish text>` | translate text |
| `sv help` | list every command (a bare `sv` goes to the game, where it usually means southwest) |
| `sv translate <text>` | translate text that starts with a command word, e.g. `sv translate demo` |
| select text, right-click → **Translate Swedish → English** | translate anything already on screen |
| `sv demo` | one sample sentence per visualization, to compare them |
| `sv views` | list the visualizations; click to switch each on or off |
| `sv view <name> [on\|off]`, `sv only <name>...` | switch visualizations from the command line |
| `sv auto [on\|off]` | translate Swedish lines arriving from the game |
| `sv save <word>`, `sv words`, `sv quiz` | personal word list and a spaced-repetition quiz |
| `sv history [n]` | the last n translations |
| `sv email <address\|off>` | raise MyMemory's free limit from 5,000 to 50,000 characters a day |
| `sv clear <history\|words\|cache>` | forget saved data |

### Visualizations

Any combination can be on at once; `inline` and `gloss` are on by default.

- **inline** - the English printed under the Swedish. With auto-translate, a line whose
  translation is already cached gets it directly beneath; otherwise translations follow the game
  text a moment later, in the same order as the lines they belong to.
- **gloss** - an interlinear, word-by-word gloss: each Swedish word above its English meaning.
- **hover** - every Swedish word becomes a link; hover for its meaning, click to save it to your word list.
- **reveal** - the English is hidden behind a link, so you translate in your head first.
- **panel** - a dockable side window keeping a running Swedish/English history.
- **subtitle** - a movie-style card over the bottom of the main window; it disappears after 5-15
  seconds, depending on length, or when clicked.

## How it works

Sentences are translated by the free [MyMemory](https://mymemory.translated.net) API, which needs no
account. Word glosses come first from a built-in dictionary of about 570 common words, then from
the cache, then from MyMemory - except for auto-translated lines, which are glossed offline only
(unknown words show as `?`) so game text cannot eat the daily quota one word at a time. Successful
translations are cached (up to 3,000 phrases), so repeated text is normally not fetched again.
Requests run three at a time, with your own requests ahead of auto-translated ones. If MyMemory
reports its quota or rate limit is reached, the package stops asking for a while instead of
retrying every line.

Auto-translate uses a heuristic to pick lines that look Swedish - words with å/ä/ö, or common
Swedish words that are rare in English - so it mostly leaves English text alone.

Settings, the cache, your word list and history are saved in `SwedishTranslator.data.lua` in the
profile directory, and survive reinstalling the package. The previous save is kept as `.bak`; if the
file is ever unreadable it is set aside rather than overwritten, and the backup is used instead.

## Building

The package is built with [muddler](https://github.com/demonnic/muddler) from the sources in `src/`
(metadata in `mfile`, scripts loaded in the order `src/scripts/SwedishTranslator/scripts.json`
lists them). With muddler installed, run in this directory:

```bash
muddle                      # writes build/SwedishTranslator.mpackage
```

or without installing anything, through muddler's Docker image:

```bash
docker run --rm -it -u $(id -u):$(id -g) -v "$PWD":"/$PWD" -w "/$PWD" demonnic/muddler
```

`SwedishTranslator.mpackage` next to this file is a copy of that build, for installing straight
from the repository.

## Known limitation

If you select text, click elsewhere to clear the selection and then right-click → **Translate**,
the previously selected text is translated. Mudlet passes the last selection's coordinates to the
menu action even after the highlight is gone, and a script has no way to tell, so select the text
again right before translating it. Reported upstream as
[Mudlet/Mudlet#11401](https://github.com/Mudlet/Mudlet/issues/11401).
