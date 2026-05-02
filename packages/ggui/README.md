# GGui

GGui is a small MKS package for terminal UI on top of `std.tty`.

It focuses on predictable widgets:
- app lifecycle: `begin()`, `finish()`, `clear()`, `flush()`;
- screen helpers: `screen(w, h)`, `is_tty()`;
- windows: `frame()`, `panel()`, `floating()`, `app_frame()`;
- widgets: `list()`, `menu()`, `button()`, `input()`, `input_box()`, `textarea()`;
- text: `text()`, `text_center()`, `paragraph()`;
- input: `event()`, `poll(ms)`.

Build:

```bash
mks pkg build packages/ggui
```

Install flat next to your app:

```text
app/
  main.mks
  ggui.mkspkg
```

Use:

```mks
using ggui as ui;

ui.begin();
ui.app_frame("Demo", "q quit");
ui.floating(10, 5, 42, 8, "Окно", "Русский and English text wrap safely.");
ui.input(4, 15, 40, "Input", "text", 4, 1);
ui.flush();
ui.event();
ui.finish();
```
