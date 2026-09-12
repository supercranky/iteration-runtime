# Iteration Runtime

A small JavaScript game runtime built with Sokol, QuickJS, NanoVG, and SoLoud. The primary target is WebAssembly/WebGL 2; native fips configurations are also retained.

## Requirements

Install these tools first:

- Git
- Python 3
- CMake 3.5 or newer
- Ninja

On macOS with Homebrew:

```sh
brew install cmake ninja
```

On Ubuntu/Debian:

```sh
sudo apt-get update
sudo apt-get install git python3 cmake ninja-build
```

Emscripten does not need to be installed globally. The setup command below installs it into the ignored `fips-sdks/` directory.

## Clone and install

Clone the repository with its pinned dependencies:

```sh
git clone --recursive https://github.com/supercranky/iteration-runtime.git
cd iteration-runtime
```

If the repository was cloned without `--recursive`, initialize it with:

```sh
git submodule update --init --recursive
```

Install and activate the Emscripten SDK:

```sh
cd iteration-sapp
python3 fips emsdk install latest
```

The local `fips` launcher supports modern Python and modern emsdk configuration files. No global fips installation is required.

## Build WebAssembly

From `iteration-sapp/`:

```sh
# Optimized build
python3 fips build sapp-webgl2-wasm-ninja-release

# Debug build
python3 fips build sapp-webgl2-wasm-ninja-debug
```

Generated files are written outside the source tree:

```text
../fips-build/iteration-sapp/<configuration>/
../fips-deploy/iteration-sapp/<configuration>/
```

Those directories and `fips-sdks/` are intentionally ignored by Git.

## Run in a browser

WASM must be served over HTTP rather than opened through a `file://` URL:

```sh
python3 -m http.server 8000 \
  --directory ../fips-deploy/iteration-sapp/sapp-webgl2-wasm-ninja-release
```

Open <http://localhost:8000/iteration-sapp.html>.

## JavaScript and assets

The runtime loads `iteration-sapp/sapp/data/index.js`. Files copied into a deployment are explicitly listed in:

```text
iteration-sapp/sapp/data/iteration-assets.yml
```

After changing `index.js` or another listed asset, rebuild the selected configuration. If a replacement file retained an older timestamp and fips does not copy it, run:

```sh
touch sapp/data/index.js
python3 fips build sapp-webgl2-wasm-ninja-release
```

## Dependency notes

- Sokol and the fips dependencies are pinned Git submodules.
- QuickJS `2026-06-04` is vendored as a minimal source set in `iteration-sapp/sapp/quickjs/sources/`.
- Build products, downloaded SDKs, deployment output, and local IDE/CMake state must not be committed.
