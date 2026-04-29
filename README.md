# EarShield

> **The ultimate audio guardian for TeamSpeak 3.**
> Earrape protection + automatic per-client volume normalization, with zero
> configuration required.

EarShield is a TeamSpeak 3 plugin that:

* **Protects your hearing** — instantly clamps screams, mic-clips, and
  earrape down to a safe ceiling using a per-sample soft-knee limiter.
* **Balances every voice** — auto-boosts quiet speakers and tames loud
  ones so everyone in your channel arrives at the same target dB.
* **Remembers per-user preferences** — overrides survive reconnects via the
  TeamSpeak unique identifier; a music bot you ignored stays ignored.
* **Updates itself** — checks for new releases on startup and offers a
  one-click in-place update without losing your settings.

Forked from [exp111/VolumeLeveler](https://github.com/exp111/VolumeLeveler);
the audio engine and feature set were rewritten for EarShield.

---

## Features

### Limiter (always on)
A per-sample soft-knee limiter with a tanh-based saturation curve. Audio
**never** exceeds the configured ceiling (default `-10 dBFS`), but unlike a
hard clipper it preserves waveform shape, so loud peaks get tamed without
the harsh "earrape at low volume" distortion of naive ceiling clamps.

### Normalization (optional)
A slow RMS auto-gain stage measured on the **original** input (not the
post-limiter signal) brings quiet speakers up to your target dB. The two
stages are deliberately decoupled in time — AGC is ~20× slower than the
limiter — so they never fight or pump.

### Per-client overrides
Right-click any client in the channel tree:

| Action | What it does |
|---|---|
| **Toggle EarShield Ignore** | Bypass EarShield entirely for this client (music bots, soundboards). |
| **EarShield Client Settings** | Override that client's limiter ceiling, normalization boost, and on/off independent of the global defaults. |

All overrides are keyed by TeamSpeak unique identifier and persisted to
`EarShield.ini`, so they survive reconnects and TS3 restarts.

### Global controls
`Plugins -> EarShield Global Settings`:

* Limiter ceiling slider (`-30 dBFS` … `0 dBFS`)
* Normalization boost slider (`0 dB` … `+30 dB`)
* Enable / disable normalization
* **Language** toggle (English / Italiano) — applied at runtime, no
  separate downloads
* Reset all native TeamSpeak per-client volume modifiers to neutral
* Reset all EarShield settings to factory defaults

### Self-updater
A few seconds after TS3 loads the plugin, EarShield fetches a small XML
feed from this branch's `raw.githubusercontent.com` URL and compares the
advertised build number against the embedded one. If a newer build is
available you are asked whether to update; the helper closes TS3, removes
old binary variants, runs the package installer, and the new version is
live on the next launch.

`EarShield.ini` is **never** touched during updates — your global limit,
normalization, language choice, and per-client overrides all carry over.

You can also force a check at any time via
`Plugins -> Check for EarShield Updates`.

### Channel-commander default
By default channel commanders are exempt from auto-boost (so a Commander
broadcast is not pushed even louder). The per-client setting overrides this
on a case-by-case basis.

---

## Installation

### TeamSpeak 3 Add-On installer (all platforms)

1. Close TeamSpeak.
2. Download the latest `EarShield.ts3_plugin` from the
   [releases page](https://github.com/gamebaiters/EarShield/releases/latest).
3. Double-click the file. The TeamSpeak Add-On Installer launches.
4. Click **Install**.
5. Launch TeamSpeak. Open `Tools -> Options -> Addons` and confirm
   "EarShield" is enabled.

Permanent latest-release URL:
`https://github.com/gamebaiters/EarShield/releases/latest/download/EarShield.ts3_plugin`

### macOS note

If `open EarShield.ts3_plugin` does nothing (TeamSpeak.app on macOS does
not always have the file association registered), unzip the package
manually with `ditto -x -k EarShield.ts3_plugin /tmp/earshield` and copy
`plugins/libearshield_mac.dylib` into
`~/Library/Application Support/TeamSpeak 3/plugins/`.

The auto-updater handles this automatically on subsequent updates.

---

## Configuration

Most users never need to touch the defaults: limiter `-10 dBFS`,
normalization on, boost `+10 dB`. If you want to tweak:

### Set the global volume

1. `Plugins -> EarShield Global Settings`.
2. Drag the **Limiter** slider to your comfortable maximum (most headsets
   sit comfortably at `-15 dBFS` … `-20 dBFS`).
3. Tick **Enable Normalization** if you want quiet speakers boosted to
   that same level. Untick it if you only want earrape protection.
4. Click **Apply & Close**.

### Customize one user

1. Right-click the user → **EarShield Client Settings**.
2. Override their limiter ceiling, boost level, and normalization toggle
   independently of the global defaults.
3. **Apply & Close**.
4. To revert that user back to global, open the same dialog and click
   **Reset to Global**.

### Bypass EarShield for a music bot

Right-click → **Toggle EarShield Ignore**. EarShield will not touch
that client's audio stream at all.

### Reset everything

`Plugins -> EarShield Global Settings -> Reset All`.
This wipes every override, deletes the in-memory state, and rewrites
`EarShield.ini` to defaults.

### Where settings live

Per platform — paths TS3 uses for plugin config:

| OS | Path |
|---|---|
| Windows | `%APPDATA%\TS3Client\plugins\EarShield.ini` |
| Linux | `~/.ts3client/plugins/EarShield.ini` |
| macOS | `~/Library/Application Support/TeamSpeak 3/plugins/EarShield.ini` |

---

## Uninstalling

### From inside TeamSpeak

`Tools -> Options -> Addons -> EarShield -> Uninstall`. EarShield is
hardened so the binary file actually unloads cleanly when this is
clicked (a long-standing bug for TS3 plugins that hold network/Qt event
queue references at shutdown).

### Full clean uninstall (recommended for switching versions)

Download the appropriate uninstall script from the latest release:

* Windows: `uninstall_earshield.bat`
* Linux: `uninstall_earshield.sh`
* macOS: `uninstall_earshield_macos.sh`

Run it from your Desktop. The script will:

1. Ask whether to delete `EarShield.ini` (default: keep) and whether to
   back up first (default: yes).
2. Close TeamSpeak.
3. Back up `plugins/`, `settings.db`, and `EarShield.ini` to a timestamped
   folder on your Desktop (if you opted in).
4. Remove every EarShield (and legacy Volume Leveler) binary variant.
5. Sweep `earshield*.log`.
6. Clean leftover Plugins entries from TeamSpeak's `settings.db` (skipped
   with a warning if `sqlite3` is not on `PATH`).
7. Optionally delete `EarShield.ini`.

---

## Building from source

### Requirements

* CMake ≥ 3.16
* Qt 5 (Core, Widgets, Gui, Network) — 5.12 or newer
* A C++17 compiler (MSVC 2019+ on Windows, GCC/Clang elsewhere)

The TeamSpeak 3 plugin SDK headers needed to build are vendored under
`include/`.

### Windows (MSVC)

```cmd
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64"
cmake --build build
```

Output: `build/EarShield_win64.dll`.

### Linux

```bash
sudo apt-get install -y build-essential cmake \
    qtbase5-dev qttools5-dev qttools5-dev-tools \
    libqt5network5 libqt5gui5 libqt5widgets5 libqt5core5a
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Output: `build/libearshield_linux_amd64.so`.

### macOS (x86_64 — TeamSpeak is Intel-only)

```bash
brew install qt@5
cmake -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES=x86_64 \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt@5)"
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Output: `build/libearshield_mac.dylib`.

On Apple Silicon hosts, build through Rosetta with Intel Homebrew at
`/usr/local`, since TS3 itself is x86_64-only and arm64 dependencies will
not link.

### Packaging the .ts3_plugin

```bash
mkdir -p stage/plugins
cp build/<binary> stage/plugins/
cp package.ini   stage/
cd stage
zip -r ../EarShield.ts3_plugin .
```

CI does all of this automatically when you push a `vX.Y.Z` tag — see
`.github/workflows/release.yml`.

---

## Releasing

```bash
# Bump src/version.h: EARSHIELD_VERSION_BUILD + EARSHIELD_VERSION_STRING
# Bump version.xml: <latestVersion> + <latestVersionString> + <url>
# Refresh release-notes.txt (user-facing only — no internal QA text)

git add src/version.h version.xml release-notes.txt
git commit -m "Release vX.Y.Z"
git push

git tag vX.Y.Z
git push origin vX.Y.Z
```

CI builds Windows, Linux, and macOS in parallel, packages the multi-platform
`.ts3_plugin`, and creates the GitHub release with all three uninstall
scripts attached.

---

## License

GPLv3 (inherited from the upstream Volume Leveler project). See
[`LICENSE`](LICENSE) once committed, or upstream history.

## Credits

* Upstream: [exp111/VolumeLeveler](https://github.com/exp111/VolumeLeveler)
* Maintainer: Marco ([@gamebaiters](https://github.com/gamebaiters))
