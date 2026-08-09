# Minimal page plugin

`example_page.c` is the smallest in-tree template for the page ABI. It owns a
small state object and exports only `draw_plugin_entry`,
`draw_plugin_cleanup`, `draw_plugin_write` and `draw_plugin_read`.

The example demonstrates decoded `TuiInputEvent` handling, frame ticks,
drawing into the host-owned `DrawPluginSurface`, and use of the optional raw
stdin callback. It intentionally has no dependency on `app.h` or global TUI
functions.

See the full [example walkthrough](../../docs/pages/example.md) for the config
format, state ownership, four ABI entry points, call sequence and instructions
for using this directory as a new-page template. The shared contract is in the
[page plugin ABI documentation](../../docs/plugin-abi.md).

The normal top-level build produces `build/plugins/example_page.so`. It can
also be built on its own:

```sh
cmake -S examples/minimal_page_plugin -B build-example
cmake --build build-example
```
