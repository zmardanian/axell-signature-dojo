# Axell Signature Dojo

The home of the Axell Signature suite - Axell's Dojo (Axell De-Reverb, Gate, Penta EQ, Duo Compressor, Velvet, Master; 3.0: connected through the shared axell-core) on Linux.

- **Plugins**: install, update, reinstall, restore earlier versions or remove - from GitHub or a folder (USB stick).
  Every archive is checked against its published SHA-256 and unpacked by a strict reader; only the plugin bundle is
  installed, and the previous version is kept as a backup.
- **Voice Profile**: record or drop a dry take. The Dojo measures loudness, background noise, pitch, where your
  esses sit, the room's decay and the low end, plans the vocal chain, then tunes it by rendering your take through
  the installed plugins (compression amounts, Velvet's LEARN level, loudness matched at every stage). It saves
  presets for Gate, Penta EQ, Duo Compressor and Velvet plus a REAPER FX chain for mixing and a tracking chain for
  recording (De-Reverb's LIVE mode and Velvet's zero-latency de-esser, about 3 ms in all) - never replacing your own files. Velvet's de-esser amount is
  set on your take: your esses end up where natural speech has them (111 voices).
- **Open in REAPER**: a new REAPER project with an armed vocal track holding your chain, in one click.
- **Step-through listening, Reference Match and confidence**: hear your take after each plugin; move your tone toward
  a reference vocal (measured on the rendered take); every measurement says how far to trust it.
- **Chains**: ten ready-made REAPER FX chains (vocals, beats, master, tracking) from the plugins' factory presets.
- **Tools**: Batch Render (many takes through one chain), Streaming Preview (what Spotify, Apple Music, YouTube,
  Tidal and Amazon Music do to your master, and how it sounds there), Backup & Move (checksum-verified, never
  overwriting).
- **Workstation**: system, plugin folder, DAWs (native and Flatpak), REAPER's plugin path and scan, PipeWire buffer,
  realtime priority, system effects. Checks only read; fixes run in your account after you confirm.

Terminal: `--check` (workstation report), `--profile <take> [--reference <vocal>] [--name N] [--reduction %] [--gate-range dB] [--save [--open]]`,
`--render <chain.RfxChain> <out folder> <take>...` (Batch Render). `AXELL_DOJO_SANDBOX=<dir>` keeps
every file the Dojo touches inside `<dir>`.

## Build and install
```
scripts/build.sh
scripts/install.sh
```
Tests: `build/DojoCoreTests`, `build/DojoPlatformTests_artefacts/Release/DojoPlatformTests` (sandboxed), and
`tools/reaper-verify.sh` (loads every chain in a portable REAPER copy).

## Licence
AGPLv3 (JUCE is used under AGPLv3). Third-party: JUCE 8.0.15 (AGPLv3), Inter font (OFL 1.1), axell-core.
The low-mid reference curve is derived from the VCTK corpus (CC BY 4.0, University of Edinburgh) and CMU ARCTIC.
Contact: axellpetre@protonmail.com
