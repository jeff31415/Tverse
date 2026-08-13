# draw_app documentation

`draw_app` is a page-oriented terminal application built on the low-level
[`tui`](../deps/corestack/src/tui/README.md) backend. It provides nine F-key
page slots loaded as hot-reloadable modules, with Canvas on F2 and a minimal
example plugin serving as the other-page template.

## Documents

| Document | Contents |
| --- | --- |
| [Page plugin ABI](plugin-abi.md) | Normative four-function contract, exchanged types, ownership, operation payloads and active-frame sequence |
| [Application lifecycle](application-lifecycle.md) | Initialization, frame dispatch, page switching, hot reload and shutdown sequence diagrams |
| [`dlfcn` page plugin design](plugin-hot-reload-plan.md) | Design rationale, build split and remaining hardening work |
| [Minimal example page](pages/example.md) | Template state, config bytes, four entry points, stdin callback and complete example sequence |
| [Plugin starter template](../example/plugin_templete/README.md) | Copyable source/CMake template plus every ABI structure, function, read tag and write tag |
| [Reusable canvas state](canvas-state.md) | Page-independent stroke lifecycle, history ownership, projection, rendering and reuse contract |
| [Canvas page](pages/canvas.md) | Canvas ABI mapping, instance ownership, lifecycle sequence, input, history, JSON and rendering |
| [Online subsystem](online.md) | Implemented wire format, Gateway/Auth/Lobby/room ownership, queue and room ABI contracts, demo API, lifecycle and integration tests |

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
- `Ctrl+R`: reload the active page module immediately.
- `Ctrl+N`: reset the Canvas document.
- `Ctrl+Z` / `Ctrl+Y`: Canvas undo / redo.
- `Ctrl+S`: finalize the current stroke and save the Canvas document to
  `canvas.json`.
- Plain ASCII 32 through 126 on F2: select that palette character.

## Source map

- [`main.c`](../main.c): configuration selection, lifetime, and main loop.
- [`app.h`](../app.h): plugin page slots, configuration and application
  interfaces.
- [`app.c`](../app.c): module lifecycle, reload polling, input dispatch,
  composition and timing.
- [`plugin.h`](../plugin.h): four-function page ABI and exchanged TUI types.
- [`plugin-abi.md`](plugin-abi.md): normative function, payload, ownership and
  lifecycle contract for that header.
- [`plugin_loader.h`](../plugin_loader.h) and
  [`plugin_loader.c`](../plugin_loader.c): generation copies, `dlopen`, symbol
  resolution and unload.
- [`plugins/canvas`](../plugins/canvas/): complete Canvas plugin source tree
  and its local CMake targets.
- [`canvas.h`](../plugins/canvas/canvas.h) and
  [`canvas.c`](../plugins/canvas/canvas.c): centered document coordinates and
  operation history.
- [`canvas_json.h`](../plugins/canvas/canvas_json.h) and
  [`canvas_json.c`](../plugins/canvas/canvas_json.c): JSON dump/load and file
  persistence for complete Canvas documents using the project parser.
- [`canvas_json_cjson.h`](../plugins/canvas/canvas_json_cjson.h) and
  [`canvas_json_cjson.c`](../plugins/canvas/canvas_json_cjson.c):
  schema-compatible cJSON implementation retained for side-by-side
  comparison.
- [`canvas_json_jansson.h`](../plugins/canvas/canvas_json_jansson.h) and
  [`canvas_json_jansson.c`](../plugins/canvas/canvas_json_jansson.c):
  schema-compatible Jansson implementation with exact signed 64-bit integers.
- [`plugin_frame.h`](../plugin_frame.h) and
  [`plugin_frame.c`](../plugin_frame.c): shared bounds-checked plugin-surface
  drawing primitives.
- [`canvas_state.h`](../plugins/canvas/canvas_state.h) and
  [`canvas_state.c`](../plugins/canvas/canvas_state.c): reusable pending-stroke
  lifecycle, interpolation, undo/redo facade, viewport projection and
  cell-buffer rendering.
- [`canvas_page.h`](../plugins/canvas/canvas_page.h) and
  [`canvas_page.c`](../plugins/canvas/canvas_page.c): Canvas ABI entry points,
  input, layout, palette and page chrome.
- [`examples/minimal_page_plugin`](../examples/minimal_page_plugin/): smallest
  page module used by the application.
- [`example/plugin_templete`](../example/plugin_templete/): buildable
  copy-and-rename starter with all four functions, operation examples and
  local CMake template.
- [`pages.c`](../pages.c): host page metadata, module paths and config bytes.
- [`tests`](../tests/): document, JSON, state, rendering and dynamic-loader
  tests.
- [`online`](../online/): minimal server/client subsystem, public wire and room
  headers, Gateway runtime, dynamic echo room, demo executables and online
  tests. See [`online.md`](online.md) for its implementation contract.
- [`draw_app.conf.example`](../draw_app.conf.example): complete optional
  configuration example.

Output extraction, additional drawing tools and editable palettes remain
outside the current implementation.
