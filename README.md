# draw_app

`draw_app` is a page-oriented terminal drawing application. The host loads
Canvas and the example pages as hot-reloadable `.so` modules through a minimal
four-function `dlfcn` ABI. It uses the `tui` and `config` libraries from
`corestack` plus cJSON and Jansson; all three dependencies are pinned as Git
submodules.

## Checkout

```sh
git clone --recurse-submodules <draw_app-repository-url>
```

For an existing checkout, initialize the dependency with:

```sh
git submodule update --init --recursive
```

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the application with:

```sh
./build/draw_app draw_app.conf.example
```

The build places page modules under `build/plugins`. Press `Ctrl+R` to reload
the active page immediately; changed module files are also detected between
frames. [`examples/minimal_page_plugin`](examples/minimal_page_plugin/) is the
small example used by the running application. The copy-and-rename
[`example/plugin_templete`](example/plugin_templete/) starter includes all ABI
functions, a standalone CMake template and a field-by-field ABI guide. The
complete Canvas implementation lives under [`plugins/canvas`](plugins/canvas/);
the repository root retains only host code and shared plugin infrastructure.

See [`docs/README.md`](docs/README.md) for the architecture, controls and
source guide. The current plugin contract is documented separately in
[`docs/plugin-abi.md`](docs/plugin-abi.md), with lifecycle sequence diagrams in
[`docs/application-lifecycle.md`](docs/application-lifecycle.md).

The first server/client implementation slice lives in [`online/`](online/).
It includes the fixed wire codec, bounded cross-thread queues, a one-thread
Gateway, Auth/Lobby/room threads, a dynamically loaded echo-room demo, client
executable, and loopback integration tests. Its accepted design is kept in
[Chinese](design_drafts/draw_and_guess_c_s/server_arch_and_proto.md) and
[English](design_drafts/draw_and_guess_c_s/server_arch_and_proto.en.md).
