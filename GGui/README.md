# GGui

GGui is a package-style terminal UI layer for MKS.

Build:

```sh
mks pkg build GGui
```

Use:

```mks
using ggui as gui;

gui.begin();
gui.app_frame("Demo", "q quit");
gui.panel(3, 3, 40, 7, "Panel", "Text body");
gui.flush();
var key =: gui.event();
gui.finish();
```

Keyboard helpers:

- `gui.event()` / `gui.key()` waits for one key.
- `gui.poll(ms)` returns one key or `null`.
- `gui.is_quit_key(key)` checks `q`, `esc`, and `ctrl_c`.
- `gui.edit_key(value, cursor, key)` delegates text editing to `std.tty`.

Layout helpers:

- `gui.rect(x, y, w, h)` creates a rectangle object.
- `gui.fit_rect(x, y, w, h)` clips a rectangle to the current screen.
- `gui.center_rect(w, h)` returns a centered, clipped rectangle.
- `gui.inset(rect, padding)` shrinks a rectangle.
- `gui.split_vertical(rect, left_w, gap)` splits a rectangle into `left/right`.
- `gui.split_horizontal(rect, top_h, gap)` splits a rectangle into `top/bottom`.
- Widgets call `fit_rect`, so package windows do not intentionally draw outside
  the terminal bounds.

Framebuffer:

```mks
var screen =: gui.screen();
screen.clear();
screen.panel(1, 1, screen.w, screen.h, "GGui");
screen.text_clip(3, 3, 20, "hello");
screen.progress(3, 5, 20, 4, 10);
screen.render();
```

`screen.render()` writes every row, so transparent terminal backgrounds are
covered by spaces.

Widgets:

- `gui.app_frame`, `gui.panel`, `gui.frame`, `gui.modal`, `gui.list`,
  `gui.input`, `gui.button`, `gui.progress`, `gui.status_bar`.

Examples:

- [demo.mks](/home/MilakyS/CMinusInterpretator/GGui/examples/demo.mks): file manager style demo.
- [snake.mks](/home/MilakyS/CMinusInterpretator/GGui/examples/snake.mks): playable snake on top of `ggui.mkspkg`.
