# GGui TUI Package

GGui is the first bundled package-style UI library for MKS terminal apps. It is
distributed as `ggui.mkspkg` and uses `std.tty` for terminal control.

## Build And Install

From the repository root:

```bash
mks pkg build packages/ggui
```

The build creates:

```text
packages/ggui/dist/ggui-0.1.0.mkspkg
packages/ggui/dist/ggui.mkspkg
```

For an app, copy the canonical artifact next to `main.mks`:

```text
app/
  main.mks
  ggui.mkspkg
```

Import:

```mks
using ggui as ui;
```

## Minimal App

```mks
using ggui as ui;

ui.begin();
ui.app_frame("Demo", "q quit");
ui.floating(10, 5, 44, 8, "Окно", "Русский and English text wrap safely.");
ui.flush();
ui.event();
ui.finish();
```

For scripts that only draw once and exit, `begin()` is optional:

```mks
using ggui as ui;

ui.clear();
ui.panel(2, 2, 40, 7, "Panel", "Long text is wrapped inside the window.");
ui.flush();
```

## API

Lifecycle:
- `ui.begin()` switches to alternate screen, enables raw input, hides cursor,
  and clears the screen.
- `ui.finish()` resets style, shows cursor, restores cooked input, and returns
  to the normal screen.
- `ui.clear()` clears the screen.
- `ui.flush()` flushes terminal output.

Screen and input:
- `ui.screen(w, h)` returns `{ w, h }` and falls back to the provided size when
  stdout is not a real terminal.
- `ui.is_tty()` returns whether stdin and stdout are terminals.
- `ui.event()` waits for one key.
- `ui.poll(ms)` returns one key or `null` after the timeout.

Layout:
- `ui.app_frame(title, status)` draws a full-screen frame and status bar.
- `ui.frame(x, y, w, h, title)` draws a titled frame.
- `ui.panel(x, y, w, h, title, body)` draws a framed text panel.
- `ui.floating(x, y, w, h, title, body)` draws a shadowed floating window.

Widgets:
- `ui.list(x, y, w, h, title, items, selected)` draws a selectable list.
- `ui.menu(x, y, w, title, items, selected)` draws a compact list menu.
- `ui.button(x, y, w, label, active)` draws a button.
- `ui.input(x, y, w, label, value, cursor, focused)` draws a one-line input
  with horizontal scrolling and a real caret.
- `ui.input_box(x, y, w, label, value, focused)` keeps the short input
  signature and places the caret at the end.
- `ui.textarea(x, y, w, h, title, value, focused)` draws a multiline input.
- `ui.status(x, y, w, value)` draws an inverted status line.
- `ui.help(x, y, w, value)` draws dimmed helper text.

## UTF-8 And Long Text

`std.tty` text widgets now clip by terminal columns instead of bytes. Russian
and English text can be mixed safely:

```mks
ui.panel(3, 3, 30, 6, "Текст", "Русский текст wraps safely with English.");
```

Use `panel`, `floating`, `textarea`, or `paragraph` for user-provided text. They
wrap inside the given rectangle and clear unused rows, so old text does not leak
through after redraws.

## Recommended App Shape

Keep state in normal MKS variables and redraw the screen after each event:

```mks
using ggui as ui;

var selected =: 0;
var running =: true;

ui.begin();
while (running) ->
    ui.app_frame("Files", "q quit");
    ui.list(3, 3, 30, 8, "Menu", ["Open", "Search", "Настройки"], selected);
    ui.flush();

    var key =: ui.event();
    if (key ?= "q" || key ?= "ctrl_c") ->
        running =: false;
    <-
<-
ui.finish();
```

This keeps the package simple: GGui draws widgets and reads keys; your app owns
state, navigation, and actions.
