# Swedish → English translator

A Mudlet package for learning Swedish while you play: translate text on demand or
automatically, and choose how the translation is shown from six visualizations.

## Install

In Mudlet, open **Toolbox → Package Manager → Install** and pick `SwedishTranslator.mpackage`,
or run `lua installPackage("/path/to/SwedishTranslator.mpackage")`. Then type `sv:demo`.

## Use

| Command | What it does |
| --- | --- |
| `sv <swedish text>` | translate text |
| select text, right-click → **Translate Swedish → English** | translate anything already on screen |
| `sv:demo` | one sample sentence per visualization, to compare them |
| `sv:views` | list the visualizations; click to switch each on or off |
| `sv:view <name> [on\|off]`, `sv:only <name>...` | switch visualizations from the command line |
| `sv:auto [on\|off]` | translate Swedish lines arriving from the game |
| `sv:save <word>`, `sv:words`, `sv:quiz` | personal word list and a spaced-repetition quiz |
| `sv:history [n]` | the last n translations |
| `sv:email <address\|off>` | raise MyMemory's free limit from 5,000 to 50,000 characters a day |
| `sv:clear <history\|words\|cache>` | forget saved data |

### Visualizations

Any combination can be on at once; `inline` and `gloss` are on by default.

- **inline** - the English printed under the Swedish. With auto-translate, cached lines get their
  translation directly beneath them.
- **gloss** - an interlinear, word-by-word gloss: each Swedish word above its English meaning.
- **hover** - every Swedish word becomes a link; hover for its meaning, click to save it to your word list.
- **reveal** - the English is hidden behind a link, so you translate in your head first.
- **panel** - a dockable side window keeping a running Swedish/English history.
- **subtitle** - a movie-style card over the bottom of the main window that fades after a few seconds.

## How it works

Sentences are translated by the free [MyMemory](https://mymemory.translated.net) API, which needs no
account. Word glosses come first from a built-in dictionary of about 570 common words, then from
MyMemory. Every result is cached, so a phrase is only fetched once, and requests are queued (three
at a time) so a burst of game text cannot flood the service. Auto-translate only picks lines that
look Swedish - containing å/ä/ö or common Swedish words - so English text is left alone.

Settings, the cache, your word list and history are saved in `SwedishTranslator.data.lua` in the
profile directory, and survive reinstalling the package.

## Building

The package is generated from the sources in `scripts/` (loaded in file-name order):

```bash
python3 build.py            # writes SwedishTranslator.mpackage next to this file
```
