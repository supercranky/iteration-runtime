# AGENTS.md

## Repository purpose

This repository builds the Iteration JavaScript runtime. The main application is in `iteration-sapp/` and targets WebAssembly/WebGL 2 through fips and Emscripten.

## Initial setup

From the repository root:

```sh
git submodule update --init --recursive
cd iteration-sapp
python3 fips emsdk install latest
```

Required host tools are Git, Python 3, CMake, and Ninja. Emscripten is installed locally under `fips-sdks/`; do not require or assume a global emsdk installation.

## Required verification

Run both WASM configurations after changing C/C++, CMake, Sokol integration, QuickJS, or dependencies:

```sh
cd iteration-sapp
python3 fips build sapp-webgl2-wasm-ninja-debug
python3 fips build sapp-webgl2-wasm-ninja-release
```

For asset-only changes, rebuilding the affected configuration is sufficient. Confirm that the deployed file matches the source when fips timestamp-based copying is involved.

Serve the release build for browser testing:

```sh
python3 -m http.server 8000 \
  --directory ../fips-deploy/iteration-sapp/sapp-webgl2-wasm-ninja-release
```

Then test <http://localhost:8000/iteration-sapp.html>. WebAudio autoplay warnings before user interaction are expected; page errors and failed requests are not.

## Source layout

- `iteration-sapp/sapp/`: application and runtime source
- `iteration-sapp/sapp/data/index.js`: JavaScript entry point loaded by QuickJS
- `iteration-sapp/sapp/data/iteration-assets.yml`: deployment asset allowlist
- `iteration-sapp/sapp/quickjs/`: minimal vendored QuickJS runtime
- `iteration-sapp/sapp/plugins/`: generic plugin ABI and runtime smoke-test plugin
- `iteration-sapp/libs/sokol/`: Sokol implementation translation units
- `sokol/`: pinned upstream Sokol headers
- `sokol-tools-bin/`: pinned shader compiler binaries
- `fips-*`: pinned build dependencies

## Generated and local-only files

Never commit:

- `fips-build/`
- `fips-deploy/`
- `fips-sdks/`
- CMake caches and generated build-system files
- object files, static libraries, WASM binaries, APKs, editor state, or Python caches

These are covered by the root `.gitignore`. If a new tool creates generated output, add an appropriate ignore rule rather than committing it.

## Dependency rules

- Keep dependency directories as Git submodules; do not vendor their generated output.
- When upgrading Sokol, update `sokol` and `sokol-tools-bin` together and migrate removed APIs in the application.
- QuickJS is intentionally vendored as only the files needed to build the embedded library. Do not add upstream tests, examples, command-line tools, generated archives, or precompiled libraries.
- Application-specific plugins and their build systems belong in the consuming application repository. Keep only the stable plugin ABI, generic host, and smoke-test plugin here.
- Do not edit files under `fips-build/`, `fips-deploy/`, or `fips-sdks/`; regenerate them.

## Coding notes

- Use public Sokol APIs. Current Sokol uses image views, separate samplers, `sg_begin_pass()`, `sglue_environment()`, and `sglue_swapchain()`.
- Use public QuickJS APIs; do not depend on internal array layouts.
- Add runtime assets to `iteration-assets.yml`; merely placing a file in `sapp/data/` does not deploy it.
