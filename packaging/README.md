# Packaging

Use [the build instructions](../docs/building.md) for a source-only playable
installation. No separately licensed Ammerow media are required for that build.

The CMake `AMMEROW_PACKAGE_PROFILE` setting controls installation:

- `development`: copies available game data and fonts; absent art/audio use
  ordinary glyphs, synthesised effects and silence for music.
- `release`: installs the exact files in `runtime.txt`, `runtime-art.txt`
  and `runtime-audio.txt`; it requires the separately supplied media.
- `audio-playtest`: a compatibility alias for `release`.

These manifests and `src/cmake/modules/AmmerowPackage.cmake` control the
installed paths. Build with the chosen profile, then install into an empty
staging directory:

```powershell
cmake --install build-public --config Release --prefix C:/staging/Ammerow
```

Before distributing a binary, supply its complete corresponding source under
a valid GPLv2 distribution route. Official releases bundle a source archive:

```powershell
./scripts/attach-corresponding-source.ps1 -PackageRoot C:/staging/Ammerow -SourceArchive C:/source/Ammerow-source.zip
./scripts/build-portable-zip.ps1 -PackageRoot C:/staging/Ammerow -OutputPath C:/packages/Ammerow.zip
```

The ZIP tool verifies extraction and file hashes. `-FlatRoot` produces a
depot-style ZIP without an enclosing folder. Never package personal saves,
settings or test output. A checksum verifies file identity, not source
completeness or licence rights.

`public-source-files.txt` lists the exact reviewed source files. New files
require an explicit entry. `utils/check-public-source.py` checks this boundary
and regenerated data; it does not establish legal clearance or prove how an
executable was compiled.
