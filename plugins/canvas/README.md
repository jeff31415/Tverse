# Canvas page plugin

This directory owns the complete Canvas page implementation:

- `canvas.c/.h` and `canvas_internal.h`: document and operation history;
- `canvas_state.c/.h`: pending strokes, projection, undo/redo and rendering;
- `canvas_json*.c/.h`: built-in, cJSON and Jansson persistence backends;
- `canvas_page.c/.h`: the four-function page-plugin ABI adapter;
- `CMakeLists.txt`: the PIC Canvas core and `canvas_page.so` module targets.

Shared plugin ABI, loading and cell-surface helpers remain at the repository
root because they are consumed by every page plugin.

See the [Canvas page documentation](../../docs/pages/canvas.md) for the entry
config, ABI operation mapping, lifecycle sequence, document history and JSON
persistence behavior. The generic rules are in the
[page plugin ABI contract](../../docs/plugin-abi.md).
