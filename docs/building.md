# Building and testing

This source builds a playable Windows x64 SDL3 game without Ammerow's
separately licensed media. Missing illustrations and sprites use ordinary
glyphs; missing effects use the synthesiser; home music is silent.
Licensed third-party fonts and their notices are included.

## Requirements

- Visual Studio with C++ desktop tools and Windows SDK (verified with VS 2026).
- CMake on PATH, or Visual Studio's bundled CMake.
- x64 SDL3, SDL3_ttf and SDL3_image development packages, including their DLLs.
  Official builds use SDL3 3.4.4, SDL3_ttf 3.2.2 and SDL3_image 3.4.2.
  Source URLs, hashes and terms are in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
- Python 3 for data checks; these checks require no third-party Python packages.

Windows SDL3 is the supported product build. Other inherited frontend options
are not release-certified. HTML documentation generation is not part of this
source package.

## Configure and build

Run from the source root, replacing the example SDL paths with your installed
development packages. Use a fresh build directory.

```powershell
cmake -S . -B build-public -A x64 `
  -DSUPPORT_SDL3_FRONTEND=ON -DSUPPORT_WINDOWS_FRONTEND=OFF `
  -DSUPPORT_HEADLESS_FRONTEND=ON -DSUPPORT_TEST_FRONTEND=ON `
  -DSUPPORT_SPOIL_FRONTEND=OFF -DSC_INSTALL=ON `
  -DAMMEROW_PACKAGE_PROFILE=development `
  -DCMAKE_PREFIX_PATH="C:/deps/SDL3;C:/deps/SDL3_ttf;C:/deps/SDL3_image"
cmake --build build-public --config Release --target OurExecutable
& './build-public/game/Release/ammerow.exe' -msdl3
```

Saves and settings stay under the executable's own `lib` directories.

## Check and install

```powershell
python utils/check-public-source.py
python -m unittest discover -s tests -p "test_*.py"
cmake --build build-public --config Release --target allunittests
cmake --build build-public --config Release --target headless-smoke
cmake --install build-public --config Release --prefix C:/staging/Ammerow
& 'C:/staging/Ammerow/ammerow.exe' -msdl3
```

The headless smoke run exercises the production game and SDL3 renderer without
opening a window. Its capture and report are under
`build-public/game/Release/headless-smoke`. Native tests include cave,
fishing, save/load and UI checks.

Before creating build outputs, `python utils/check-public-source.py --tree`
also verifies that an extracted archive contains exactly its listed files.
The checker verifies file boundaries, documentation links and generated data;
it is not an IP/legal clearance tool.

Use the `development` install profile for source-only builds. The `release`
profile requires the separately supplied media in the runtime manifests.
See [Packaging](../packaging/README.md) and [Data authoring](data-authoring.md).
