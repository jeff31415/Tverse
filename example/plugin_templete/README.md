# Page plugin starter template

This directory is the copy-and-rename starter for a `draw_app` page plugin.
It intentionally keeps the requested directory name `plugin_templete`. The
code is complete and buildable: [`plugin_templete.c`](plugin_templete.c)
exports all four symbols required by ABI v1, and
[`CMakeLists.txt`](CMakeLists.txt) builds it as a hidden-visibility `.so`.

The normative contract remains [`plugin.h`](../../plugin.h) and the full
[ABI document](../../docs/plugin-abi.md). This README explains every public
type declared by `plugin.h` and shows how the starter handles each operation.

## Build the unchanged starter

From the `draw_app` root, either build it with the whole project:

```sh
cmake -S . -B build
cmake --build build --target draw_page_plugin_templete
```

or configure this directory by itself:

```sh
cmake -S example/plugin_templete -B build-plugin-templete
cmake --build build-plugin-templete
```

Both forms produce `plugins/plugin_templete.so` below the selected build
directory. The standalone form accepts these cache variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `DRAW_APP_SOURCE_DIR` | two directories above this one | Tree containing `plugin.h`, `plugin_frame.c` and `deps/corestack` |
| `DRAW_APP_PLUGIN_OUTPUT_DIR` | `<build>/plugins` | Destination for the module |

The module is not registered as an application page merely by building it.
To load a copied plugin, add its canonical `.so` path, title, shortcut and
configuration bytes to the host registration table in
[`pages.c`](../../pages.c).

## ABI at a glance

The host loads the module with `dlopen`, resolves four fixed symbol names and
calls them synchronously on the main thread:

```text
draw_plugin_entry    create private state
draw_plugin_write    host -> plugin lifecycle, input and time
draw_plugin_read     plugin -> host frame output
draw_plugin_cleanup  destroy private state before dlclose
```

`read` and `write` are named from the host's point of view:

```text
host -- draw_plugin_write(...) --> plugin
host <-- draw_plugin_read(...)  -- plugin output in host-owned storage
```

No ABI call transfers ownership of a payload pointer. Copy values that must
survive a call. Do not retain `DrawPluginOpenArgs *`, `config.data`, a write
payload, `DrawPluginSurface *`, or `surface.cells`.

## Every type declared by `plugin.h`

### ABI version and symbol visibility

`DRAW_PLUGIN_ABI_VERSION` is the exact version accepted by entry. ABI v1 has
no struct-size negotiation, so a different value must return
`TG_ERR_UNSUPPORTED`. `DRAW_PLUGIN_EXPORT` gives the four functions default
ELF visibility while CMake hides all other symbols.

### `DrawPlugin`

```c
typedef struct DrawPlugin DrawPlugin;
```

This is an incomplete, opaque type. The plugin allocates any concrete private
state it wants—`PluginTemplete` in the starter—and casts that pointer to
`DrawPlugin *`. The host stores and passes the pointer back but never reads
its fields. The plugin owns it from a successful entry until cleanup.

### `DrawPluginStdinReadFn`

```c
typedef size_t (*DrawPluginStdinReadFn)(
    void *userdata,
    void *destination,
    size_t capacity);
```

This optional host callback is a pull-style raw-byte input channel. The
plugin supplies writable storage and a capacity; the callback copies and
consumes no more than that many bytes and returns the copied count. Zero means
that no raw bytes are currently available, not end-of-file.

Call it only on the main thread while handling the active instance's
`INPUT`, `TICK`, or `READ_FRAME` call. Do not call it during entry, enter,
leave, or cleanup. Do not assume it is non-null. The starter copies the host
table in entry and reads at most 32 raw bytes during each tick.

Raw bytes and decoded events are separate copies. Reading raw bytes does not
cancel an already decoded `TuiInputEvent`, and receiving `WRITE_INPUT` does
not consume bytes from this raw ring.

### `DrawPluginHost`

```c
typedef struct DrawPluginHost {
    void *userdata;
    DrawPluginStdinReadFn stdin_read;
} DrawPluginHost;
```

| Field | Definition and ownership |
| --- | --- |
| `userdata` | Opaque host value. Pass it unchanged as the callback's first argument; never dereference or free it. |
| `stdin_read` | Optional raw stdin callback. Check for null before calling. |

The `DrawPluginHost` value inside open arguments is borrowed. Copy the two
fields into private state if callbacks will be used later, as the starter
does. The callback table stays usable until cleanup under ABI v1.

### `DrawPluginOpenArgs`

```c
typedef struct DrawPluginOpenArgs {
    unsigned abi_version;
    TgSizei frame_size;
    TgBytes config;
    DrawPluginHost host;
} DrawPluginOpenArgs;
```

| Field | Definition and required handling |
| --- | --- |
| `abi_version` | Exact host ABI version. Compare with `DRAW_PLUGIN_ABI_VERSION` before creating state. |
| `frame_size` | Initial drawable page width and height, excluding host chrome. Reject non-positive components. |
| `config` | Plugin-defined borrowed bytes. `data` may be null only when `len == 0`; bytes need not be aligned, textual or NUL-terminated. |
| `host` | Borrowed host callback table described above. |

The argument structure and config storage expire when entry returns. The
starter treats config as a title, bounds it to 63 bytes, copies it and appends
its own NUL. A real plugin should define its own versioned config layout and
validate it before use.

### `DrawPluginFrameContext`

```c
typedef struct DrawPluginFrameContext {
    uint64_t frame_index;
    double delta_time;
    TgSizei frame_size;
} DrawPluginFrameContext;
```

| Field | Definition and required handling |
| --- | --- |
| `frame_index` | Number of frames successfully presented by the host. It is observation data, not an instruction to increment once. |
| `delta_time` | Monotonic elapsed seconds since the previous frame. Use it for time-based updates; do not retain its address. |
| `frame_size` | Current drawable dimensions. A changed value is ABI v1's resize notification. |

This is the borrowed payload of `WRITE_TICK`. The starter validates the size
and nonnegative/non-NaN delta, then copies the frame index and size into its
state.

### `DrawPluginSurface`

```c
typedef struct DrawPluginSurface {
    TgSizei size;
    TuiCell *cells;
} DrawPluginSurface;
```

| Field | Definition and required handling |
| --- | --- |
| `size` | Positive width and height of the output surface. It determines the valid coordinate range. |
| `cells` | Mutable host-owned row-major array of exactly `size.w * size.h` cells. Cell `(x, y)` is at `y * size.w + x`. |

This is the mutable borrowed payload of `READ_FRAME`. A plugin may update the
cells during that call only. It must not free, reallocate or retain either
pointer. Always bounds-check coordinates and guard multiplication if indexing
directly. The starter uses `plugin_frame_fill` and `plugin_frame_text`, whose
implementations perform the bounds checks.

The host retains the surface between frames. A dirty-rendering plugin may
leave unchanged cells alone and return `TG_OK`; the starter redraws the full
surface every time for clarity.

### `DrawPluginLeaveReason`

```c
typedef enum DrawPluginLeaveReason {
    DRAW_PLUGIN_LEAVE_SWITCH = 0,
    DRAW_PLUGIN_LEAVE_RELOAD,
    DRAW_PLUGIN_LEAVE_SHUTDOWN
} DrawPluginLeaveReason;
```

| Value | Meaning |
| --- | --- |
| `DRAW_PLUGIN_LEAVE_SWITCH` | Another page becomes active. This instance may be entered again later. |
| `DRAW_PLUGIN_LEAVE_RELOAD` | This generation is being replaced. Cleanup follows after the replacement is ready. |
| `DRAW_PLUGIN_LEAVE_SHUTDOWN` | The application is terminating. Cleanup follows. |

The starter validates the enum and clears its `active` flag. Persistent page
state should normally survive `SWITCH`; transient focus/gesture state may be
discarded for any leave reason.

### `DrawPluginWriteKind`

```c
typedef enum DrawPluginWriteKind {
    DRAW_PLUGIN_WRITE_ENTER = 0,
    DRAW_PLUGIN_WRITE_LEAVE,
    DRAW_PLUGIN_WRITE_INPUT,
    DRAW_PLUGIN_WRITE_TICK
} DrawPluginWriteKind;
```

This enum is the tag for `draw_plugin_write`. The tag fixes the payload type;
there is no generic byte-buffer parsing:

| Tag | Exact `data` contract | Starter behavior |
| --- | --- | --- |
| `DRAW_PLUGIN_WRITE_ENTER` | Must be `NULL` | Marks the instance active. |
| `DRAW_PLUGIN_WRITE_LEAVE` | Borrowed `const DrawPluginLeaveReason *` | Validates the reason and marks the instance inactive. |
| `DRAW_PLUGIN_WRITE_INPUT` | Borrowed `const TuiInputEvent *` | Copies the decoded event into private state. |
| `DRAW_PLUGIN_WRITE_TICK` | Borrowed `const DrawPluginFrameContext *` | Copies timing/size state and optionally pulls raw stdin bytes. |

Unknown tags, missing required payloads and non-null `ENTER` payloads return
`TG_ERR_INVALID`.

### `DrawPluginReadKind`

```c
typedef enum DrawPluginReadKind {
    DRAW_PLUGIN_READ_FRAME = 0
} DrawPluginReadKind;
```

This enum tags `draw_plugin_read`. ABI v1 has one output operation:

| Tag | Exact `data` contract | Starter behavior |
| --- | --- | --- |
| `DRAW_PLUGIN_READ_FRAME` | Borrowed mutable `DrawPluginSurface *` | Validates it and renders state into host-owned cells. |

Unknown tags, null instances and malformed surfaces return `TG_ERR_INVALID`.

### Shared types imported from `tui.h`

`plugin.h` includes `tui.h`, so the ABI also exchanges these concrete project
types directly:

| Type | Relevant model |
| --- | --- |
| `TgResult` | `TG_OK` on success; use `TG_ERR_INVALID`, `TG_ERR_NOMEM`, `TG_ERR_UNSUPPORTED`, or another project error as appropriate. |
| `TgSizei` | Two signed 32-bit members, `w` and `h`. ABI surfaces require both to be positive. |
| `TgBytes` | `const uint8_t *data` plus `size_t len`; no implicit ownership or terminator. |
| `TuiInputEvent` | Decoded key, text, or mouse value selected by its `type` tag. Copy the value if retained. |
| `TuiCell` | UTF-8 bytes, display width, foreground/background colors and style bits for one terminal cell. |

Host and plugin must use the same pinned headers and compatible compiler,
architecture, enum layout and structure layout. Changing one of these
exchanged layouts requires an ABI version bump and rebuilding all modules.

## Every required ABI function

### `draw_plugin_entry`

```c
TgResult draw_plugin_entry(
    const DrawPluginOpenArgs *args,
    DrawPlugin **out_plugin);
```

Entry creates one independent instance but does not activate it. Required
order:

1. reject a null `out_plugin`;
2. immediately set `*out_plugin = NULL`;
3. validate `args`, ABI version, dimensions and config consistency;
4. allocate and completely initialize private state;
5. copy every borrowed value needed later;
6. publish the opaque pointer and return `TG_OK` only after full success.

On failure, release partial resources and keep output null. Every successfully
returned instance later receives cleanup, even if it is never entered.

### `draw_plugin_cleanup`

```c
void draw_plugin_cleanup(DrawPlugin *plugin);
```

Cleanup is the final, infallible release operation. Stop plugin-owned work,
release all private resources and make no more host callbacks. It must work
for inactive and never-entered instances. The host invokes it before
`dlclose`, exactly once for each successfully created instance. The starter's
only resource is its state allocation, so it calls `free`.

### `draw_plugin_write`

```c
TgResult draw_plugin_write(
    DrawPlugin *plugin,
    DrawPluginWriteKind kind,
    const void *data);
```

This is a synchronous tagged write from host to plugin. First validate the
opaque instance, then switch on `kind`, cast `data` only inside the matching
case, validate it, and copy values needed later. Never guess a payload type
from its contents and never retain the pointer.

Normal active-frame order is zero or more `INPUT` writes followed by one
`TICK`. `ENTER` starts an active interval; `LEAVE` ends it. An instance may
receive repeated enter/leave cycles. Unknown tags must fail rather than being
silently ignored.

### `draw_plugin_read`

```c
TgResult draw_plugin_read(
    DrawPlugin *plugin,
    DrawPluginReadKind kind,
    void *data);
```

This is a synchronous tagged request for output from plugin to host. Validate
the instance and tag before casting the mutable payload. For `READ_FRAME`,
validate surface dimensions and `cells`, write only within its row-major
bounds, return `TG_OK`, and retain nothing from the call.

Do not return a plugin-owned cell array. The direction is represented by
mutating host-owned storage passed in `data`.

## Copying the template

1. Copy this directory and preferably correct `templete` to your plugin name.
2. Rename `plugin_templete.c`, `PluginTemplete`, the CMake target and
   `OUTPUT_NAME` consistently.
3. Replace the private state and define the exact `TgBytes config` layout.
4. Keep the four exported names and signatures unchanged.
5. Keep all four write cases and the frame read case, even when an operation
   is an intentional validated no-op.
6. Remove the raw stdin read if decoded events are sufficient.
7. Add plugin-specific loader tests and register the module in `pages.c` only
   when it should appear in the application.

Do not include `app.h`, call global TUI drawing/input APIs, or store global
mutable page state. Rendering should target `DrawPluginSurface` directly or
use the shared bounds-checked helpers from `plugin_frame.h`.
