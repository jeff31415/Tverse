# draw_app documentation

`draw_app` is a page-oriented terminal application built on the low-level
[`tui`](../deps/corestack/src/tui/README.md) backend. It currently provides nine F-key pages,
an application-level page lifecycle, and an implemented Canvas page on F2.

## Documents

| Document | Contents |
| --- | --- |
| [Application lifecycle](application-lifecycle.md) | Main loop, page switching, ownership, principal structures and application functions |
| [Reusable canvas state](canvas-state.md) | Page-independent stroke lifecycle, history ownership, projection, rendering and reuse contract |
| [Canvas page](pages/canvas.md) | F2 layout, input flow, coordinate system, operation history, rendering and page functions |

The original Canvas design material is under
[`design_drafts/canvas`](../design_drafts/canvas/).

## Startup and configuration

Run with built-in defaults:

```sh
./build/draw_app
```

Or pass a configuration file:

```sh
./build/draw_app draw_app.conf.example
```

Defaults are assigned explicitly before an optional file is loaded:

| Directive | Default | Meaning |
| --- | ---: | --- |
| `tui_width` | 120 | Total TUI width in cells |
| `tui_height` | 36 | Total TUI height, including the footer |
| `canvas_width` | 48 | Width of the centered final-output region |
| `canvas_height` | 20 | Height of the centered final-output region |
| `target_fps` | 30 | Main-loop frame limit |

The configuration file uses the `name=value` syntax documented by
[`config`](../deps/corestack/src/config/README.md). Width, height, and FPS must be positive.
Height must leave at least one row above the fixed one-row footer.

## Shortcuts

Commands use Control-modified shortcuts. Page switching is the sole exception
and uses plain function keys to avoid conflicts with terminal and desktop
shortcuts:

- `F1` through `F9`: switch page.
- `Ctrl+Q`: exit.
- `Ctrl+N`: reset the Canvas document.
- `Ctrl+Z` / `Ctrl+Y`: Canvas undo / redo.
- `Ctrl+S`: finalize the current stroke and save the Canvas document to
  `canvas.json`.
- Plain ASCII 32 through 126 on F2: select that palette character.

## Source map

- [`main.c`](../main.c): configuration selection, lifetime, and main loop.
- [`app.h`](../app.h): page, frame, configuration, and application interfaces.
- [`app.c`](../app.c): allocation, input dispatch, composition, timing, and frame
  helpers.
- [`canvas.h`](../canvas.h) and [`canvas.c`](../canvas.c): centered document
  coordinates and operation history.
- [`canvas_json.h`](../canvas_json.h) and
  [`canvas_json.c`](../canvas_json.c): JSON dump/load and file persistence for
  complete Canvas documents using the project parser.
- [`canvas_json_cjson.h`](../canvas_json_cjson.h) and
  [`canvas_json_cjson.c`](../canvas_json_cjson.c): schema-compatible cJSON
  implementation retained for side-by-side comparison.
- [`canvas_json_jansson.h`](../canvas_json_jansson.h) and
  [`canvas_json_jansson.c`](../canvas_json_jansson.c): schema-compatible
  Jansson implementation with exact signed 64-bit integers.
- [`canvas_frame.h`](../canvas_frame.h) and
  [`canvas_frame.c`](../canvas_frame.c): shared bounds-checked page-frame
  drawing primitives.
- [`canvas_state.h`](../canvas_state.h) and
  [`canvas_state.c`](../canvas_state.c): reusable pending-stroke lifecycle,
  interpolation, undo/redo facade, viewport projection and cell-buffer
  rendering.
- [`canvas_page.h`](../canvas_page.h) and
  [`canvas_page.c`](../canvas_page.c): F2 input, layout, palette and page chrome.
- [`pages.c`](../pages.c): page registration and placeholder pages.
- [`tests`](../tests/): document history, state, projection and rendering tests.
- [`draw_app.conf.example`](../draw_app.conf.example): complete optional
  configuration example.

Output extraction, additional drawing tools and editable palettes remain
outside the current implementation.
