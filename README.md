# dosbundle

`dosbundle` is a DOS-to-desktop bundler for macOS and Linux.

The project goal is to take:

- a directory tree that will become the emulated `C:\`
- a `dosbox` configuration file
- a DOS batch file or executable to launch

and produce a distributable application.

## Warning

This project was vibe coded in just a few hours. The code is fairly benign,
but use your judgement. You're free to not use it, but it works for me.

## License

The project is MIT licensed. I figured this is the most flexible as it allows
use of the bundler with commercial software and not require the commercial software
to become GPL.

## Build

The project uses `CMake` as the portable build definition. `Ninja` is the
recommended generator, although any suitable CMake generator can be used.

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/dosbundle --help
./build/dosbundle bundle examples/bundle.toml
./build/dosbundle launch artifacts/Screamr2-stage --dry-run
./build/dosbundle package artifacts/Screamr2-stage artifacts/Screamr2-sfx
./artifacts/Screamr2-sfx --dry-run --extract-root /path/to/extract-root
```

Optional user configuration can be placed in `~/.config/dosbundle.conf` or
`$XDG_CONFIG_HOME/dosbundle.conf`:

```ini
dosbox_path=/usr/local/bin/dosbox-staging
tar_path=/usr/bin/tar
extract_root=/tmp/dosbundle
```
## Current Staged Layout

The `bundle` command currently writes a directory tree like:

```text
<output>/
├── dosbox/
│   └── dosbox.conf
├── manifest/
│   ├── bundle.toml
│   ├── launch.properties
│   └── summary.txt
├── payload/
│   └── c/
└── startup/
    └── <startup file>
```

## Self-Extracting Format

The `package` command produces a single executable by:

- copying the launcher binary
- appending a `tar.gz` archive of the staged bundle
- appending a small binary footer with archive metadata

When the packaged executable is run directly, it:

- extracts the embedded bundle into a temporary directory
- discovers `dosbox-staging`
- launches the extracted bundle through the normal launcher path
- removes the temporary extraction directory on exit

Use `--extract-root <path>` to force extraction under a specific directory,
which makes it possible to point extraction at a RAM disk later. If no CLI
override is provided, `DOSBUNDLE_EXTRACT_ROOT` is used as a fallback.

Configured defaults are loaded before fallback discovery:

- `dosbox_path` overrides `dosbox-staging` discovery
- `tar_path` overrides the tar executable used for package/extract steps
- `extract_root` sets the default extraction directory
