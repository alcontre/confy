# confy

Cross-platform C++17/wxWidgets desktop app for loading an XML config and downloading selected source/artifact components.

## Current status

- CMake project scaffolded
- wxWidgets app shell with `File -> Load Config...`
- Basic component checklist rendering
- Initial XML parsing into a strongly-typed model
- `Apply` action stubbed for next iteration

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

Or use the one-stop clean rebuild script:

```bash
./build.sh
```

## Run

```bash
./build/confy
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Next implementation steps

1. Replace regex-based parser with a proper XML parser backend.
2. Add source/artifact subsection controls per component (enable flags + dropdowns).
3. Implement async download workers (git shallow clone + nexus fetch).
4. Add credentials/settings persistence to user config.
