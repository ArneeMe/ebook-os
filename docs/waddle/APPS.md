# Script apps

Apps that come from the waddleos repo instead of the firmware. They run on the
reader and the T5 in Lua 5.4 (`lib/lua`), and can only draw, read input, keep
files in their own folder and, when they ask for it, fetch from the web. The
runtime is in `src/apps/store/`.

## Where they come from

- `apps/index.json` in this repo lists the apps, and `apps/<id>/` holds each one.
- On the device, **Apper → Hent flere apper** fetches the list (only then; nothing
  is checked by itself). A tap installs or updates an app; holding one you have,
  here or in Apper, removes it with its data.
- Installed apps live on the SD card in `/Apper/<id>/`. Their files are in
  `/Apper/<id>/data/`.

To publish an app: add its folder, add it to `apps/index.json` and merge into
`waddle`. Raise `version` in both places when it changes, so the devices offer
"Oppdater".

`index.json`:

```json
{"apps": [{"id": "bokhylle", "name": "Bokhylle", "description": "…", "version": 1,
           "devices": ["x4pro", "t5"], "files": ["app.json", "main.lua"]}]}
```

`app.json`: `name`, `description`, `version`, `devices` (`"x4pro"`, `"t5"`;
left out = both), and `"net": true` for an app that uses `net`, `"books": true`
for one that saves books.

## main.lua

The app defines the functions it needs; all are optional.

| Function | When |
|---|---|
| `start()` | when it opens |
| `draw()` | after `app.redraw()`, and once at the start; the screen is not cleared for you |
| `tap(x, y)` | a tap |
| `press(x, y)`, `move(x, y)`, `release(x, y)` | a finger or pen down, moving, up (for drawing) |
| `button(name)` | `"up"`, `"down"`, `"left"`, `"right"`, `"ok"`, `"next"`, `"prev"` (also swipes left and right) |
| `tick()` | every `app.every(ms)` |
| `stop()` | when it closes |

Back (or the home gesture) always closes the app.

**screen** (draw only in `draw()`): `width()`, `height()`, `clear()`,
`header(title)` (returns the y where content starts), `text(x, y, text, opts)`
(returns its width), `textWidth(text, opts)`, `lineHeight(size)`,
`wrap(text, width, opts)` (a table of lines), `line(x1, y1, x2, y2, opts)`,
`rect(x, y, w, h, opts)`, `dot(x, y, size, opts)`. `opts` is a table:
`size` (`"small"`, `"normal"`, `"big"`), `bold`, `white`, `width`, `radius`,
`fill`, `lines`.

**app**: `redraw(mode)` (`"fast"`, `"half"` or `"full"` refresh), `every(ms)`
(0 stops), `close()`, `millis()`, `time()` (unix seconds, 0 when the clock is
not set), `device()`.

**storage** (its own data folder; plain file names): `read(name)` (nil when
missing), `write(name, text)`, `remove(name)`, `list()`.

**net** (only with `"net": true`; joins the saved Wi-Fi, which goes off again
when the app closes): `get(url)` (body, or nil and why), `download(url, name)`
(into the data folder).

**books** (only with `"net"` and `"books"`): `download(url, "Tittel.epub")` into
`/Books`, `has("Tittel.epub")`.

`print(...)` goes to the serial log.

## The apps

- **Bokhylle** (`apps/bokhylle`): the books in `books/` (`index.tsv`: file,
  title, author, bytes, tab-separated), added with `waddle book --hylle
  <checkout>`. Only books without copyright: the repo is public.

## Limits

2 MB of memory (PSRAM), 256 KB for `main.lua` and for each file or answer.
A call that runs for more than 5 seconds is stopped ("appen svarer ikke"), and
any error shows on the screen instead of the app. There is no `io`, `os`,
`require`, `load` or debug library, and only text scripts are loaded.

A call that waits for the web blocks the screen: draw "Henter …" first, then
fetch from `tick()`.
