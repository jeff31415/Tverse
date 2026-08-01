# draw_app

`draw_app` is a page-oriented terminal drawing application. It uses the
`tui` and `config` libraries from `corestack`, pinned as a Git submodule.

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

See [`docs/README.md`](docs/README.md) for the architecture, controls and
source guide.
