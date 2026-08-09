# Application and plugin lifecycle

The `draw_app` executable owns terminal access, timing, page selection,
host-side surfaces and dynamic loading. Every page is a `MODULE` library and
the host interacts with it only through the four functions in
[`plugin.h`](../plugin.h). The exact function, type, ownership and result-code
rules are defined by the [page plugin ABI contract](plugin-abi.md).

## Main loop

```mermaid
flowchart TD
  Config["Assign defaults and load optional config"] --> Init["app_init"]
  Init --> TUI["Initialize TUI"]
  TUI --> Pages["Allocate page slots and load all modules"]
  Pages --> Enter["ENTER initial page"]
  Enter --> Reload["Poll canonical module stamps"]
  Reload --> Poll["Poll TUI events"]
  Poll --> Dispatch["Consume host commands and route page input"]
  Dispatch --> Stop{"Still running?"}
  Stop -- yes --> Tick["TICK active plugin"]
  Tick --> Read["READ_FRAME into host surface"]
  Read --> Compose["Copy surface and draw footer"]
  Compose --> Present["Present, increment frame and rate limit"]
  Present --> Reload
  Stop -- no or error --> Shutdown["Leave active page and unload all modules"]
  Shutdown --> Restore["Restore terminal"]
```

All ABI calls are synchronous on the main thread. Only the active page receives
input, tick and frame-read calls. Inactive page instances remain loaded and
preserve state until selected, reloaded or destroyed.

## Page slot ownership

Each host-owned `Page` contains:

| Field group | Owner | Purpose |
| --- | --- | --- |
| title, shortcut, module path | host | Footer and routing metadata |
| config storage | host | Stable copy passed as borrowed `TgBytes` during entry |
| `DrawPluginModule` | host/loader | `dlopen` handle, opaque instance, four resolved functions and generation path |
| `DrawPluginSurface` | host | Persistent page-sized `TuiCell` allocation |
| generation and file stamps | host | Automatic reload observation and retry suppression |
| active and entered flags | host | Selection and lifecycle bookkeeping |

The plugin owns only the concrete object behind `DrawPlugin *` and any
resources reachable from it. It never receives `App *` or `Page *`.

## Initialization and module entry

`app_init` initializes TUI first, then `app_register_default_pages` creates all
nine page slots. F2 loads `canvas_page.so`; F1 and F3-F9 each load a separate
instance of `example_page.so`. Reusing one canonical module does not share
plugin state because every slot gets its own generation copy and entry call.

```mermaid
sequenceDiagram
  participant Main
  participant App
  participant TUI
  participant Loader
  participant Plugin as each page module

  Main->>App: app_init(config)
  App->>TUI: tui_init(screen size)
  TUI-->>App: TG_OK
  loop each page definition
    App->>App: allocate surface and copy config
    App->>Loader: module_open(path, DrawPluginOpenArgs)
    Loader->>Plugin: draw_plugin_entry(args, out instance)
    Plugin-->>Loader: TG_OK and DrawPlugin pointer
    Loader-->>App: handle, functions and instance
  end
  App->>Plugin: write(ENTER, NULL) for page zero
  Plugin-->>App: TG_OK
  App-->>Main: initialized application
```

Entry creates an instance but does not activate it. The initial page receives
enter only after all registrations succeed. If any allocation, load, symbol
resolution or entry fails, `app_init` takes the common shutdown path and
cleans up every previously created slot.

## One active frame

`main.c` invokes the application stages in a fixed order. The host polls for
module changes before polling input, so a successful automatic reload is in
place before that frame's events are dispatched.

```mermaid
sequenceDiagram
  participant Main
  participant App
  participant TUI
  participant Plugin as active plugin
  participant Surface as active page surface

  Main->>App: app_begin_frame()
  App->>App: poll reload stamps for every page
  App->>TUI: tui_poll_events()
  TUI-->>App: ordered events
  Main->>App: app_dispatch_events()
  loop each event not consumed by host
    App->>Plugin: write(INPUT, event pointer)
    Plugin-->>App: TgResult
  end
  Main->>App: app_update_active_page()
  App->>Plugin: write(TICK, frame context pointer)
  Plugin-->>App: TgResult
  Main->>App: app_render_active_page()
  App->>Plugin: read(FRAME, surface pointer)
  Plugin->>Surface: draw changed cells
  Plugin-->>App: TgResult
  Main->>App: app_compose()
  App->>Surface: copy cells to TUI back buffer
  App->>TUI: draw global footer
  Main->>App: app_end_frame()
  App->>TUI: tui_present()
  App->>App: increment frame index and rate limit
```

Any error returned by input, tick, frame read or present breaks the main loop
and proceeds to shutdown. `Ctrl+Q` sets the running flag false and skips tick
and rendering for that frame.

## Ordered input and page switching

The host consumes three classes of global input:

- `Ctrl+Q` stops the application;
- `Ctrl+R` forces reload of the active slot;
- plain F1-F9 selects a page.

All other `TuiInputEvent` values go to the active plugin in original order. A
switch occurs immediately within the event loop, so later events from the same
TUI poll go to the new page.

```mermaid
sequenceDiagram
  participant TUI
  participant Host
  participant Old as old page plugin
  participant New as new page plugin

  TUI-->>Host: function-key event
  Host->>Old: write(LEAVE, SWITCH reason pointer)
  alt old leave succeeds
    Old-->>Host: TG_OK
    Host->>Host: update active index and flags
    Host->>New: write(ENTER, NULL)
    alt new enter succeeds
      New-->>Host: TG_OK
      TUI-->>Host: later event from same batch
      Host->>New: write(INPUT, event pointer)
    else new enter fails
      New-->>Host: error
      Host->>Host: leave frame loop and shut down
    end
  else old leave fails
    Old-->>Host: error
    Host->>Host: abort switch and shut down
  end
```

Page switching is not a snapshot transaction. A new-page enter failure is a
normal frame error and does not re-enter the old page; shutdown follows.

## Reload transaction

Automatic reload compares canonical module modification time and size. A new
stamp must be observed identically twice. Each changed stamp is attempted once
until the canonical file changes again. `Ctrl+R` bypasses the stability wait
for the active page.

The candidate is copied to a unique temporary generation, opened with
`RTLD_NOW | RTLD_LOCAL`, resolved and entered before the old handle is closed.

```mermaid
sequenceDiagram
  participant Host
  participant Loader
  participant Old as current generation
  participant New as candidate generation

  Host->>Loader: module_open(canonical path, open args)
  Loader->>New: dlopen and resolve four symbols
  Loader->>New: draw_plugin_entry(args, out instance)
  alt load, symbols or entry fail
    Loader->>New: cleanup if a non-null instance exists
    Loader->>New: dlclose and unlink generation
    Loader-->>Host: error
    Host->>Old: keep current instance unchanged
  else candidate entry succeeds
    Loader-->>Host: candidate module
    alt page slot is active
      Host->>New: write(ENTER, NULL)
      alt candidate enter fails
        New-->>Host: error
        Host->>Loader: close candidate
        Host->>Old: continue current instance unchanged
      else candidate enter succeeds
        New-->>Host: TG_OK
        Host->>Old: write(LEAVE, RELOAD reason pointer)
        Note over Host,Old: old leave is best effort during reload
        Host->>Host: swap module and clear host surface
        Host->>Loader: close old module
        Loader->>Old: draw_plugin_cleanup(instance)
        Loader->>Loader: dlclose and unlink old generation
      end
    else page slot is inactive
      Host->>Host: swap module and clear host surface
      Host->>Loader: close old module
      Loader->>Old: draw_plugin_cleanup(instance)
      Loader->>Loader: dlclose and unlink old generation
    end
  end
```

A successful reload deliberately starts with fresh plugin state. There is no
snapshot or restore ABI. Canvas JSON save files are explicit documents and are
not automatic reload snapshots. A reload candidate failure is logged but does
not stop the application.

## Shutdown

Only the active instance receives `DRAW_PLUGIN_LEAVE_SHUTDOWN`. The host then
destroys every slot in reverse registration order, including instances that
were loaded but never entered. TUI remains alive until all plugin cleanup has
finished.

```mermaid
sequenceDiagram
  participant Main
  participant Host
  participant Active as active plugin
  participant Plugin as each plugin in reverse order
  participant Loader
  participant TUI

  Main->>Host: app_shutdown()
  opt active instance is entered
    Host->>Active: write(LEAVE, SHUTDOWN reason pointer)
    Active-->>Host: result ignored during shutdown
  end
  loop every page slot in reverse registration order
    Host->>Loader: module_close(module)
    Loader->>Plugin: draw_plugin_cleanup(instance)
    Loader->>Loader: dlclose(handle) and unlink generation
    Host->>Host: free config copy and surface cells
  end
  Host->>TUI: tui_shutdown()
  Host-->>Main: shutdown complete
```

## Failure and lifetime rules

1. Candidate reload errors retain the current generation; normal ABI-call
   errors end the main loop.
2. Cleanup always precedes `dlclose`, and no resolved function pointer is used
   afterward.
3. A plugin must support cleanup without enter and repeated enter/leave cycles.
4. The host owns every exchanged payload and surface; the plugin borrows them
   only for the current call.
5. Undefined behavior in a plugin remains process-level undefined behavior.
   The loader isolates bad build artifacts, not crashes or memory corruption.
6. No plugin may call global TUI functions; the raw stdin callback is the only
   host service in ABI v1.

The implementation rationale and future hardening list remain in
[`plugin-hot-reload-plan.md`](plugin-hot-reload-plan.md).
