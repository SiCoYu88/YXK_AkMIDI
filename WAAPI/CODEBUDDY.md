# CODEBUDDY.md This file provides guidance to CodeBuddy when working with code in this repository.

## Common commands

- `cd waapi-python-tools` — Run tool commands from the add-on root so relative paths in examples match the repository layout.
- `py -3 -m pip install waapi-client scipy` — Install runtime dependencies used by the WAAPI scripts; `scipy` is required by `auto-trim-sources`.
- `py -3 <tool-directory> --help` — Show CLI options for a single tool, for example `py -3 .\auto-trim-sources --help`.
- `py -3 .\auto-create-events [GUID ...]` — Create `Play_` Events for selected or passed playable objects, mirroring the Actor-Mixer work unit hierarchy under `\Events`.
- `py -3 .\auto-midi-map [GUID]` — Structure a selected Blend Container by note names in child sound names and set MIDI key tracking/filter properties.
- `py -3 .\auto-rename-container [GUID]` — Rename a selected container to the common prefix shared by its children.
- `py -3 .\auto-trim-sources [GUID ...] --threshold_end -45` — Analyze source WAV files and write trim/fade properties through WAAPI.
- `py -3 .\auto-adjust-sound-voice-loudness --language "English(US)" --loudness integrated` — Compare non-reference Sound Voice loudness against the project reference language and set `VolumeOffset`.
- `py -3 .\report-language-duration-inconsistency --min_threshold 80 --max_threshold 120` — Report missing localized Sound Voice sources or duration ratios outside thresholds.
- `py -3 .\new-synth-one [GUID] --count=10` — Generate random Synth One Sound SFX objects near the selected Actor-Mixer location.
- `py -3 .\text-to-speech --original "<WwiseProjectOriginals>" <GUID ...>` — Generate WAVs from object notes via Windows PowerShell speech synthesis and import them into Wwise.
- `Get-ChildItem -Recurse -Filter __main__.py | ForEach-Object { py -3 -m py_compile $_.FullName }` — Syntax-check all Python entry points. There is no dedicated test suite in this repository.
- `py -3 -m py_compile .\auto-trim-sources\__main__.py` — Syntax-check one tool after editing it; use the target tool path as the single-script smoke test.

## Architecture overview

This repository contains `waapi-python-tools`, a collection of standalone Python command add-ons for Audiokinetic Wwise. The code assumes a running Wwise instance with Wwise Authoring API enabled and communicates through `waapi-client` using the default WAAPI connection. The add-on root is meant to be copied under `%APPDATA%\Audiokinetic\Wwise\Add-ons`, where Wwise discovers `Commands/waapi-python-tools.json`.

`Commands/waapi-python-tools.json` is the integration layer between Wwise UI and Python. It registers context-menu commands, chooses the executable (`py.exe`), passes Wwise variables such as `${id}`, `${CurrentCommandDirectory}`, and `${WwiseProjectOriginals}`, and controls selection behavior (`SingleSelectionSingleProcess`, `MultipleSelectionSingleProcessSpaceSeparated`, etc.). When adding a user-facing tool, update this JSON only if the tool should appear in Wwise menus; otherwise a directory with `__main__.py` can still be run manually with `py -3 <tool-directory>`.

Each tool is isolated in its own folder and uses a `__main__.py` entry point, so directories are executable Python modules. There is no shared package layer: helper functions, argument parsing, WAAPI calls, and error handling live inside each script. Favor small local helpers over cross-tool coupling unless introducing a shared module is clearly worthwhile. Most scripts follow the same shape: parse `argparse` CLI arguments, open `with WaapiClient() as client`, resolve explicit GUID arguments or fall back to `ak.wwise.ui.getSelectedObjects`, query Wwise objects with `ak.wwise.core.object.get` or WAQL, then mutate the project with `object.set`, `object.create`, `object.move`, `object.setProperty`, `object.setName`, or `audio.import`.

Several tools perform Wwise project mutations and should preserve undo behavior when practical. `auto-midi-map`, `auto-trim-sources`, `auto-adjust-sound-voice-loudness`, and `new-synth-one` wrap changes with `ak.wwise.core.undo.beginGroup` / `endGroup`. If editing mutation flows, keep operations batched in the existing `set_args["objects"]` patterns where possible so Wwise changes remain coherent and efficient.

Tool responsibilities are domain-specific. `auto-create-events` maps playable Actor-Mixer selections to `Play_<name>` Events and reconstructs WorkUnit/Folder hierarchy under `\Events`. `auto-midi-map` parses note names from child object names, creates Random/Sequence Containers for duplicate notes, and sets MIDI note-tracking properties. `auto-rename-container` computes a common child-name prefix and applies it as the container name. `auto-trim-sources` uses `scipy.io.wavfile` to inspect original WAV samples, find threshold/zero-crossing trim positions, and set source trim/fade plus optional sound initial delay.

Localization tools operate on Sound Voice objects and project language metadata. `report-language-duration-inconsistency` compares each localized voice duration against the reference language and prints missing sources or ratio violations. `auto-adjust-sound-voice-loudness` compares `loudness.momentaryMax` or `loudness.integrated` against the reference language and writes the calculated difference to `VolumeOffset` (Make-up Gain) for localized voices.

Generation tools create or import assets. `new-synth-one` builds nested dictionaries for Sound SFX objects with Synth One source plug-ins, RTPCs, modulators, random curves, and project ShareSet effects, then creates them with `ak.wwise.core.object.set`. `text-to-speech` reads selected object notes, calls `text-to-speech/speak.ps1` on Windows to synthesize WAV files, then imports them with `ak.wwise.core.audio.import`; it depends on the `--original` path normally supplied by the Wwise command add-on.

There is no packaging metadata, formatter, linter, or automated test suite checked in. Validation is mostly by running `--help`, `py_compile`, and testing against an open Wwise project. Be careful when running scripts: most are not dry-run tools and can modify the Wwise project immediately.