# Canvas page (F2)

Canvas is the stateful F2 implementation of the shared page-plugin ABI. Its
four exported functions and page chrome live in
[`canvas_page.c`](../../plugins/canvas/canvas_page.c). The linked-list document
model is in [`canvas.h`](../../plugins/canvas/canvas.h) and
[`canvas.c`](../../plugins/canvas/canvas.c), with persistence in
[`canvas_json.h`](../../plugins/canvas/canvas_json.h) and
[`canvas_json.c`](../../plugins/canvas/canvas_json.c); reusable stroke
maintenance, projection and rendering are in
[`canvas_state.h`](../../plugins/canvas/canvas_state.h) and
[`canvas_state.c`](../../plugins/canvas/canvas_state.c). Their complete
ownership, lifecycle and reuse contract is documented in
[Reusable canvas state](../canvas-state.md).

The original layout sketch is available as
[`note_canvas_design.png`](../../design_drafts/canvas/note_canvas_design.png).

The generic function signatures, payload lifetime and result-code rules are
defined by the [page plugin ABI contract](../plugin-abi.md).

## ABI binding and ownership

`canvas_page.so` exports only `draw_plugin_entry`, `draw_plugin_cleanup`,
`draw_plugin_write` and `draw_plugin_read`. The internal
`canvas_page_create`, event, update and render functions are hidden behind
those four calls.

### Entry config

Canvas defines its `TgBytes config` as exactly one binary `TgSizei`:

```c
TgSizei output_size = app->config.canvas_output_size;
TgBytes config = {
    .data = (const uint8_t *)&output_size,
    .len = sizeof(output_size),
};
```

This is distinct from `DrawPluginOpenArgs.frame_size`:

| Value | Meaning |
| --- | --- |
| `args.frame_size` | Current terminal page area, excluding the global footer |
| binary config `TgSizei` | Centered final-output width and height stored in the Canvas document |

Entry rejects a null config, a length other than `sizeof(TgSizei)`, or any
non-positive output/frame dimension. It initializes `CanvasState`, copies the
host callback table and frame size, fills the ASCII palette and selects `#`.
The current Canvas implementation retains the host callbacks for ABI
consistency but does not call the raw stdin reader.

### Instance ownership

```text
CanvasPage
├── CanvasState
│   ├── CanvasDocument
│   │   └── CanvasHistory and owned operations/samples
│   └── CanvasStroke pending samples
├── CanvasPalette
├── CanvasLayout
├── copied DrawPluginHost and frame size
└── transient mouse, dirty and status values
```

The plugin owns that complete tree. The host separately owns the
`DrawPluginSurface` cells passed to frame reads. Cleanup destroys
`CanvasState` and its recursively owned history before freeing `CanvasPage`;
it does not require the instance to have received enter first.

### Tagged operation mapping

| ABI operation | Canvas implementation | Effect |
| --- | --- | --- |
| `ENTER` with `NULL` | `canvas_page_on_enter` | Compute layout, reveal palette selection and dirty the frame |
| `LEAVE` with reason pointer | `canvas_page_on_leave` | Finalize a pending stroke for switch, reload or shutdown |
| `INPUT` with event pointer | `canvas_page_handle_event` | Route commands, palette text and mouse actions |
| `TICK` with frame context pointer | `canvas_page_update` | Track size and recompute layout after resize |
| `FRAME` with surface pointer | `canvas_page_render` | Rebuild the borrowed surface only when dirty |

Canvas validates every tag and required pointer. `LEAVE` also validates the
reason range even though all three current reasons share the same stroke
finalization behavior.

### Canvas call sequence

```mermaid
sequenceDiagram
  participant Host
  participant ABI as Canvas ABI adapter
  participant State as CanvasState
  participant Doc as CanvasDocument
  participant Surface as host-owned surface

  Host->>ABI: entry(args with output-size config, out instance)
  ABI->>State: canvas_state_init(output size)
  State->>Doc: canvas_document_init(output size)
  ABI-->>Host: TG_OK and CanvasPage instance
  Host->>ABI: write(ENTER, NULL)
  ABI->>ABI: compute layout and mark frame dirty
  loop ordered active-page input
    alt canvas mouse press
      Host->>ABI: write(INPUT, mouse event pointer)
      ABI->>State: begin_stroke(world position, selected cell)
    else canvas mouse drag
      Host->>ABI: write(INPUT, mouse event pointer)
      ABI->>State: append_stroke(world position)
    else mouse release or finalizing command
      Host->>ABI: write(INPUT, event pointer)
      ABI->>State: finalize_stroke()
      State->>Doc: commit one DRAW_CELLS operation
    else palette or command input
      Host->>ABI: write(INPUT, event pointer)
      ABI->>ABI: select, new, save, undo or redo
    end
  end
  Host->>ABI: write(TICK, frame context pointer)
  opt frame size changed
    ABI->>ABI: recompute responsive layout
  end
  Host->>ABI: read(FRAME, surface pointer)
  opt frame is dirty
    ABI->>Surface: fill page and draw toolbox plus Canvas border
    ABI->>State: canvas_state_render(target)
    State->>Doc: replay operations from head through cursor
    Doc-->>State: applied CanvasSample values
    State->>Surface: draw backgrounds, history and pending stroke
    ABI->>Surface: draw palette and toolbar
  end
  ABI-->>Host: TG_OK
  Host->>ABI: write(LEAVE, reason pointer)
  ABI->>State: finalize pending stroke if present
  Note over Host,ABI: switches may later ENTER the same instance
  Host->>ABI: cleanup(instance) on reload or shutdown
  ABI->>State: canvas_state_destroy()
```

The surface is a persistent render cache. A clean frame read returns `TG_OK`
without touching its cells. Input, layout changes and history changes mark the
cache dirty.

A successful hot reload creates a fresh `CanvasPage`; it does not transfer the
document or pending stroke. `Ctrl+S` JSON persistence is explicit and remains
independent from reload. The old active instance receives `LEAVE_RELOAD`
before cleanup, which finalizes its pending stroke in memory but does not
implicitly save it.

## Current scope

The page implements:

- a large visible canvas whose outer cells act as a draft area;
- a centered, configurable final-output region with a subtly different
  background;
- an ASCII 32 through 126 character palette;
- mouse press/drag drawing with the selected character;
- linked-list operation history with undo, redo and replay;
- JSON document dump/load and an enabled Save action;
- placeholder additional tools.

Output extraction, extra tools and palette editing are reserved for later work.

## Page event and render flow

```mermaid
flowchart TD
    Event["Ordered TuiInputEvent"] --> Kind{"Event kind"}
    Kind -- "Ctrl command" --> Command["New, Save, Undo or Redo"]
    Kind -- "plain ASCII" --> Select["Select matching palette character"]
    Kind -- "mouse" --> Hit{"Hit test"}
    Hit -- "palette" --> Select
    Hit -- "toolbar" --> Command
    Hit -- "canvas press or drag" --> Stroke["CanvasState builds a pending world-coordinate stroke"]
    Hit -- "release" --> Commit["CanvasState commits one DRAW_CELLS operation"]
    Command --> Dirty["Mark frame dirty"]
    Select --> Dirty
    Stroke --> Dirty
    Commit --> Dirty
    Dirty --> Render["Rebuild page frame when render runs"]
    Render --> StateRender["canvas_state_render"]
    StateRender --> Base["Paint draft and final-output backgrounds"]
    Base --> Replay["Replay history through cursor"]
    Replay --> Pending["Overlay pending stroke"]
    Pending --> Chrome["Draw toolbox, palette and toolbar"]
```

## Layout and coordinate system

`CanvasLayout` divides the page frame into:

- an optional left toolbox;
- a central Canvas panel and its drawable viewport;
- an optional right palette;
- a one-row Canvas toolbar above the global application footer.

Narrow terminals hide side panels before allowing the central Canvas panel to
become invalid.

All document operations use world coordinates centered on the final-output
region. They never store terminal positions or backing-array indices:

```text
world_x = screen_x - origin_screen_x
world_y = screen_y - origin_screen_y

output_min_x = -floor(output_width / 2)
output_min_y = -floor(output_height / 2)
```

The output interval is half-open:

```text
[output_min_x, output_min_x + output_width)
[output_min_y, output_min_y + output_height)
```

Y increases downwards to match terminal coordinates. Because history stores
world coordinates, changing the draft viewport or surrounding panel widths
only changes projection; it does not move existing content.

`canvas_viewport_screen_to_world()` is used for mouse input.
`canvas_viewport_world_to_screen()` clips replayed samples to the current
viewport.
`canvas_document_contains_output()` decides which background hint applies and
which cells will belong to the future final output.

## Document and history model

The operation list, rather than a `TuiCell` matrix, is the authoritative
document state:

```mermaid
flowchart LR
    Head["history.head"] <--> Applied1["applied operation"]
    Applied1 <--> Cursor["history.cursor"]
    Cursor <--> Redo1["redo operation"]
    Redo1 <--> Tail["history.tail"]
```

The page frame is a derived render cache. Each draw operation owns an array of
`CanvasSample` values; every sample contains a world position and a complete
`TuiCell`. This keeps replay independent from the current viewport and leaves
room for later foreground/background/style palette support.

Undo moves `history.cursor` to `prev`. Redo moves it to `next`. Committing
after undo frees the old redo chain before attaching the new operation.
Replay walks from `head` through `cursor`, so the same API can later drive
animation or output generation.

## Principal document structures

### `CanvasSample`

| Field | Meaning |
| --- | --- |
| `position` | Center-relative world coordinate |
| `cell` | Character, width, colors and style written at that coordinate |

### `CanvasHistory`

| Field | Meaning |
| --- | --- |
| `head`, `tail` | Bounds of the doubly linked operation list |
| `cursor` | Last currently applied operation; `NULL` means all operations are undone |
| `operation_count` | Number of nodes, including a possible redo branch |

### `CanvasOperation`

`CanvasOperation` is deliberately opaque in `canvas.h` and defined privately
in `canvas_internal.h`. A node stores:

- a `CanvasOperationType`;
- `prev` and `next` links;
- an owned `CanvasSample` array and its count.

Only `CANVAS_OPERATION_DRAW_CELLS` exists today. Keeping the type discriminator
on every node allows later line, fill or other semantic operations to share
the replay list without changing its ownership rules.

### `CanvasDocument`

Owns the configured `output_size`, its `CanvasHistory`, and a monotonically
increasing `revision` in the inclusive range `0..INT64_MAX`. Revision updates
saturate at `INT64_MAX`, and documents outside that range are invalid. Draft
samples outside `output_size` remain in the same history and are not discarded.

### JSON persistence

`canvas_document_dump_json()` serializes every document field. `history.head`
contains the oldest operation and each `next` field directly nests the newer
operation until `null`. The reverse-only `prev`, `tail` and `cursor` pointers
are stored as zero-based node references (or `null`) and rebuilt by
`canvas_document_load_json()`.

Both directions validate the full doubly linked chain. Cycles, broken `prev`
links, mismatched `tail`/`operation_count`, invalid cursor references and sample
count mismatches are rejected. `TuiCell.ch` is encoded as all eight bytes so a
dump/load cycle does not lose embedded NUL or non-ASCII bytes.

Three implementations are kept temporarily:

| Implementation | Dump | Load | Notable limit |
| --- | --- | --- | --- |
| `canvas_json.c` | Project writer | Project parser | Revision at most `INT64_MAX`; nesting limit 4096 |
| `canvas_json_cjson.c` | cJSON tree with raw integer nodes | cJSON parser | Parsed numbers at most `2^53 - 1`; default nesting limit 1000 |
| `canvas_json_jansson.c` | Jansson tree | Jansson parser | Exact signed 64-bit integers; configured Jansson nesting limit |

All dump functions produce byte-identical compact JSON, including field order,
and the tests cross-load every implementation's output through all three
loaders. The cJSON dump uses raw number nodes, so an `INT64_MAX` revision is
emitted without precision loss; its load function returns
`TG_ERR_UNSUPPORTED` rather than silently rounding values above its exact
`double` range. The Canvas page's `Ctrl+S` currently uses the project
implementation, while the cJSON and Jansson file APIs remain available for
comparison.

The UI save path is synchronous:

```mermaid
sequenceDiagram
  participant Host
  participant Canvas as Canvas page
  participant State as CanvasState
  participant JSON as project JSON backend
  participant File as canvas.json

  Host->>Canvas: write(INPUT, Ctrl+S event pointer)
  Canvas->>State: finalize_stroke()
  opt a pending stroke exists
    State->>State: commit pending samples into document history
  end
  Canvas->>JSON: canvas_document_save_json_file(document, path)
  JSON->>JSON: validate chain and dump from oldest head through next
  JSON->>File: write compact JSON bytes
  File-->>JSON: I/O result
  JSON-->>Canvas: TgResult
  Canvas->>Canvas: update Saved or Save failed status
  Canvas-->>Host: TG_OK unless stroke finalization failed
```

The public dump/load APIs support round trips and backend comparison, but the
current page UI exposes save only; it does not bind a load command yet.

## Reusable canvas state

`CanvasState` is independent of `Page`, `CanvasPage`, terminal input and the F2
layout. Another page can own one, feed it world-coordinate samples, and render
it into any `TuiCell` buffer by supplying a viewport and colors. This section
describes how F2 integrates it; see
[Reusable canvas state](../canvas-state.md) for the complete module API,
ownership rules, lifecycle and reuse example.

### `CanvasStroke`

Owns the dynamically growing pending `CanvasSample` array, the complete
`TuiCell` copied into each sample, and active/count/capacity metadata. Appending
a point fills the cells between the previous point and the new point with
Bresenham interpolation and suppresses adjacent duplicates.

### `CanvasState`

| Field | Meaning |
| --- | --- |
| `document` | Linked-list history and configured final-output size |
| `pending_stroke` | Uncommitted stroke rendered above applied history |

### Rendering and projection values

| Structure | Role |
| --- | --- |
| `CanvasViewport` | Drawable screen rectangle plus the screen cell representing world `(0, 0)` |
| `CanvasRenderStyle` | Default foreground and draft/output hint backgrounds |
| `CanvasRenderTarget` | Destination size, `TuiCell` buffer, viewport and render style |

`canvas_state_render()` first clears only the viewport to the appropriate
draft/output backgrounds, then replays applied history, then overlays the
pending stroke. A sample that keeps `TUI_COLOR_DEFAULT` inherits the render
target's foreground and region background; explicit sample colors are
preserved.

### Page-private structures

These structures are private to `canvas_page.c`:

| Structure | Role |
| --- | --- |
| `CanvasPalette` | Fixed ASCII entries, selected index and scroll row |
| `CanvasLayout` | Panel/view rectangles, world origin, palette grid and button hit boxes |
| `CanvasPage` | Reusable `CanvasState`, palette, layout, status and dirty flags |

## Principal document functions

| Function | Role |
| --- | --- |
| `canvas_document_init` | Validate output size and initialize an empty document |
| `canvas_document_destroy` | Free the entire operation chain |
| `canvas_document_reset` | Clear history while retaining configured output size |
| `canvas_document_commit_draw` | Copy a stroke into a new operation and truncate redo when needed |
| `canvas_document_can_undo/redo` | Query toolbar and command availability |
| `canvas_document_undo/redo` | Move the applied-history cursor |
| `canvas_document_replay` | Visit every applied sample in chronological order |
| `canvas_document_contains_output` | Test a world coordinate against the final-output bounds |

These functions do not depend on TTY state and are covered by
the [`tests`](../../tests/) directory.

## Principal state functions

| Function | Role |
| --- | --- |
| `canvas_state_init/destroy/reset` | Own and release a complete reusable state |
| `canvas_state_begin_stroke` | Finalize any previous stroke, copy a `TuiCell`, and add the first sample |
| `canvas_state_append_stroke` | Append interpolated world-coordinate samples |
| `canvas_state_finalize_stroke` | Commit the pending samples as one history node |
| `canvas_state_cancel_stroke` | Discard the pending samples without changing history |
| `canvas_state_stroke_active` | Query whether an uncommitted stroke exists |
| `canvas_state_can_undo/redo` | Query history movement availability |
| `canvas_state_undo/redo` | Finalize a pending stroke, then move the history cursor |
| `canvas_viewport_screen_to_world` | Convert a clipped screen coordinate to centered world space |
| `canvas_viewport_world_to_screen` | Project and clip a world coordinate into a viewport |
| `canvas_state_render` | Render backgrounds, applied history and the pending stroke to a cell buffer |

## Principal page functions

### Plugin ABI

| Function | Role |
| --- | --- |
| `draw_plugin_entry` | Validate ABI/config, allocate Canvas state and seed the ASCII palette |
| `draw_plugin_cleanup` | Destroy `CanvasState` and free plugin-owned state |
| `draw_plugin_write` | Dispatch enter, leave, input and tick operations |
| `draw_plugin_read` | Render a frame into the borrowed host surface |

The initial selected character is `#`. Space is displayed as `SP` in the
palette but retains ASCII value 32.

### Lifecycle callbacks

| Function | Role |
| --- | --- |
| `canvas_page_on_enter` | Compute layout, reveal the selection and invalidate rendering |
| `canvas_page_on_leave` | Finalize a non-empty pending stroke |
| `canvas_page_handle_event` | Route Ctrl commands, plain text and mouse events |
| `canvas_page_update` | Recompute layout when the page frame size changes |
| `canvas_page_render` | Rebuild the page frame only when dirty |
| `canvas_page_destroy` | Destroy its `CanvasState` and free page state |

### Input and command helpers

| Function group | Role |
| --- | --- |
| `canvas_palette_select`, `canvas_palette_hit` | Keyboard/mouse character selection |
| `canvas_page_viewport` | Adapt current page layout to a reusable `CanvasViewport` |
| `canvas_page_finalize_stroke` | Finalize through `CanvasState` and invalidate the page frame |
| `canvas_page_handle_mouse` | Palette, toolbar and Canvas hit testing |
| `canvas_page_new_document/save_document/undo/redo` | Implement document commands and status updates |

Changing the selected character finalizes an active stroke first, ensuring one
operation never mixes palette characters. All stroke allocation, interpolation
and history mutation occur through `canvas_state_*`; the page controller does
not maintain its own sample array.

### Rendering helpers

| Function group | Role |
| --- | --- |
| `plugin_frame_put/fill/text/box` | Shared bounds-checked primitives from `plugin_frame.c` |
| `canvas_page_draw_canvas` | Box the panel and invoke `canvas_state_render` |
| `canvas_page_draw_palette` | Render the ASCII grid and selection highlight |
| `canvas_page_draw_toolbox` | Render the current Draw tool and future-tools placeholder |
| `canvas_page_draw_toolbar` | Render actions, availability and status |

The final-output hint background is a presentation concern and is not written
into document history when a sample uses the default background color.

## Input behavior

| Input | Result |
| --- | --- |
| Plain ASCII 32-126 | Select corresponding palette entry |
| Palette left click | Select clicked entry |
| Canvas left press/drag | Draw with selected entry |
| Mouse release | Commit the pending stroke |
| Palette wheel | Scroll when the palette is shorter than its entries |
| `Ctrl+N` | Reset the document |
| `Ctrl+Z` | Finalize the current stroke, then undo |
| `Ctrl+Y` | Finalize the current stroke, then redo |
| `Ctrl+S` | Finalize a pending stroke and save `canvas.json` |
| `F1`-`F9` | Leave the page and switch pages |

Mouse release is accepted even outside the Canvas viewport. Drag samples
outside the visible viewport are ignored, while already committed draft
samples remain stored when they later become invisible.
