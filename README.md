<p align="center">
  <img src="assets/banner.png" alt="NoteCraft" width="640">
</p>

<p align="center">
  <b>A Book and Quill notepad for your Linux desktop.</b><br>
  Looks and behaves like Minecraft's writable book: the same 114&nbsp;px lines, 14 lines per page, 100 pages, 1&nbsp;023 characters per page.<br>
  A small native C program. No Electron, no web view, no GUI toolkit.
</p>

<p align="center">
  <a href="https://github.com/danielkrinets/NoteCraft/actions/workflows/ci.yml"><img src="https://github.com/danielkrinets/NoteCraft/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT">
  <img src="https://img.shields.io/badge/C11-SDL2-informational" alt="C11 + SDL2">
  <a href="README.ru.md">Русский</a>
</p>

<p align="center">
  <img src="assets/demo.gif" alt="Demo: typing, page limit, page turn, Cyrillic, signing, opening a file" width="640">
</p>

## Download

Prebuilt binaries are on the **[Releases page](https://github.com/danielkrinets/NoteCraft/releases/latest)**:

| Platform | File |
|---|---|
| **Arch Linux** (x86_64) | `notecraft-1.1.0-1-x86_64.pkg.tar.zst` — `sudo pacman -U notecraft-*.pkg.tar.zst` |
| **macOS, Apple Silicon** (M1 and newer) | `NoteCraft-1.0.1-macos-apple-silicon.dmg` (or `.zip`) |
| **macOS, Intel** | `NoteCraft-1.0.1-macos-intel.dmg` (or `.zip`) |
| Any other Linux (x86_64, aarch64) | `notecraft-1.0.1-linux-<arch>.tar.gz` (needs `libSDL2`) |

Arch users can also build from source with [`packaging/arch/PKGBUILD`](packaging/arch/PKGBUILD): `makepkg -si`.

**macOS first launch:** the app is not signed with an Apple Developer ID, so Gatekeeper will warn about it. Drag NoteCraft to *Applications*, then right-click → *Open*, or run `xattr -dr com.apple.quarantine /Applications/NoteCraft.app`. SDL2 is linked statically; nothing else has to be installed.

## Features

- **Just the book.** The window *is* an open book: two pages side by side on a leather cover, nothing else around it. Page arrows, `Page 1 of 4`, the blinking `_` caret, blue text selection, **Sign** / **Done** buttons and the "Enter Book Title" screen. Pages **turn with an animation**: the leaf lifts, swings over the spine and casts a shadow.
- **Vanilla limits** (Java Edition): 100 pages · 1023 characters per page · 14 lines per page · 114 px line width · titles up to 15 characters. Typing past a limit is simply refused, exactly like in the game.
- **Vanilla word wrap.** The wrapping algorithm is a port of Minecraft's `StringSplitter`: break at the last space, overhanging trailing space, long words are cut between characters.
- **Pixel font with Cyrillic.** A proportional Minecraft-style font drawn from scratch (Latin, digits, punctuation, full Russian alphabet incl. `Ёё`, `«»`, `—`, `№`, `€`…).
- **Plain text (`.txt`).** Open and save UTF-8 text. Pages are separated by a form feed; text without page breaks is flowed into pages automatically.
- **ePub.** Export your book as an EPUB 3 (one XHTML file per page) and open `.epub` files: books made by NoteCraft keep their exact pages, any other ePub is converted to text and paginated.
- **Pages.** Two pages per spread. Turn with the arrows, `PgUp`/`PgDn` or the mouse wheel (the last arrow adds a new spread); `Tab` or a click moves between the left and right page.
- **Signing.** *Sign* a book to give it a title and lock it (read-only view like a Written Book). *Unsign* from the menu to edit again.
- **Native and tiny.** Software-rendered into a low-resolution framebuffer and scaled by an integer GUI scale (like Minecraft's *Auto* setting), so pixels stay crisp on any display, including HiDPI. It redraws only when something changes, so idle CPU use is negligible.
- **Layout-independent shortcuts.** `Ctrl+C`/`V`… work on the Russian keyboard layout too (shortcuts follow the physical key).
- `F2` screenshot (saved to `~/Pictures/NoteCraft/`), `F11` fullscreen, drag & drop a file onto the window.

## Screenshots

| | |
|---|---|
| ![Writing](assets/screenshots/book.png) | ![Selection](assets/screenshots/selection.png) |
| ![Cyrillic](assets/screenshots/cyrillic.png) | ![Signing](assets/screenshots/sign.png) |
| ![Menu](assets/screenshots/menu.png) | ![Open file](assets/screenshots/files.png) |

## Build from source

NoteCraft needs a C compiler, **SDL2** and **zlib**.

```sh
# Debian / Ubuntu
sudo apt install build-essential libsdl2-dev zlib1g-dev
# Fedora
sudo dnf install gcc make SDL2-devel zlib-devel
# Arch
sudo pacman -S base-devel sdl2 zlib

git clone https://github.com/danielkrinets/NoteCraft
cd NoteCraft
make
./notecraft                 # run it in place
sudo make install           # optional: /usr/local/bin + desktop entry + icon
```

```
notecraft [--scale N] [file.txt | file.epub]
```

## Controls

| Key | Action |
|---|---|
| `Esc` | Game menu / back |
| `Ctrl+S` / `Ctrl+Shift+S` | Save / Save As (`.txt`) |
| `Ctrl+O` / `Ctrl+N` | Open / New book |
| `Ctrl+E` | Export as ePub |
| `Ctrl+Q` | Quit (asks about unsaved changes) |
| `PgUp` / `PgDn`, arrows, mouse wheel | Previous / next spread |
| `Tab` | Jump between the left and right page |
| `Ctrl` + `+` / `-` | Resize the window |
| `←` `→` `↑` `↓`, `Home`, `End` | Move the caret (with `Ctrl`: by word / to page start or end) |
| `Shift` + movement, `Ctrl+A`, double click | Select |
| `Ctrl+C` / `X` / `V` | Copy / cut / paste (a paste that does not fit is cut to what fits) |
| `F2` / `F11` | Screenshot / fullscreen |

## File formats

**`.txt`** — UTF-8. Pages are separated by a form feed (`U+000C`) and every page ends with a newline, so the file stays readable in any editor. A file without form feeds is flowed into pages the way the book would wrap it. On import tabs become four spaces, CR/LF is normalised, and control characters and `§` (which the game cannot type either) are dropped. More than 100 pages of text is truncated, with a warning.

**`.epub`** — export writes EPUB 3 plus an NCX table of contents for old readers; the title comes from signing (or the file name) and the author from `$NOTECRAFT_AUTHOR` / `$USER`. Import reads any DRM-free ePub (ZIP with stored or deflate entries): files created by NoteCraft are restored page for page, everything else is reduced to text (images and styling are dropped) and paginated.

## How "1:1" is it?

- **Limits** were checked against the [Minecraft Wiki](https://minecraft.wiki/w/Book_and_Quill) (Java Edition).
- **Text metrics and behaviour** (`114×126` text box, line height 9, word wrap, edit validation, word-wise caret movement, selection drawn with `OR_REVERSE`, 300 ms caret blink, the signing screen texts) follow Java Edition 1.20's `BookEditScreen` as I know it. I have **not** diffed it pixel by pixel against a real client. The vanilla editor shows a single page; the two-page spread, the cover and the page-turn animation are NoteCraft's own design in the same style.
- **Textures and font are original work**, drawn in code in Minecraft's manner (hard-edged tones, 1px outlines, sparse noise), so they are close but not identical to Mojang's.

## Development

```sh
make test        # 400 unit/end-to-end checks, no SDL needed
make demo        # headless renderer: screenshots, banner, demo frames
tools/make_media.sh   # regenerate everything in assets/ (needs python3 + ffmpeg)
```

The core (`src/*.c` except `platform_sdl.c`) does not depend on SDL: the whole application renders into a plain framebuffer, which is how the screenshots and the demo are produced from the real code. The tests also run under ASan/UBSan in CI. ePub output is structure-checked (ZIP layout, well-formed XML, manifest/spine) but has not been run through `epubcheck`.

```
src/app.c        screens, input, file dialogs        src/epub.c   ePub export/import
src/layout.c     Minecraft word wrap                 src/zipio.c  ZIP reader/writer
src/field.c      text editing (TextFieldHelper)      src/txt.c    .txt + pagination
src/font.c       pixel font                          src/theme.c  generated textures
```

## Legal

NoteCraft is an unofficial fan project. It is not affiliated with, endorsed by or sponsored by Mojang Studios or Microsoft. *Minecraft* is a trademark of Mojang AB. No Minecraft assets or code are included.

## License

[MIT](LICENSE)
