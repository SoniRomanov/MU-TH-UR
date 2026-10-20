# MU/TH/UR

**The Meridian Signal — a story told entirely through a ship's terminal.**
A native C++ Windows game with green phosphor, incoming transmissions, and
an unsettlingly calm computer. You are a human communications officer aboard
the survey ship **Kestrel**. An expedition missing for nineteen years has
started transmitting from a planet outside your assigned route.

The first playable episode takes you from intercepting that signal through
mission diversion, orbital reconnaissance, and a landing team's investigation
of Station 04. It ends at the expedition's transmitter. The deeper investigation
and rescue are not implemented yet.

## Run

Run `build.bat`, then open `build\MU-TH-UR.exe`. Requires Visual Studio 2022 or
newer with **Desktop development with C++**. No downloaded libraries, API keys,
accounts, or network connection are needed.

The app opens fullscreen. **Escape** returns to a window; **F11** toggles fullscreen.

Alternatively, with CMake and a Windows C++ toolchain:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Play

Start with `SIGNAL`. The terminal tells you what to investigate next. Everything
appears in one scrolling transcript: computer reports, messages from the crew,
and your own commands. There are no separate exploration screens or character
controls. The landing team acts on your instructions and reports back.

- **Enter** submits a command. During a transmission, it reveals the remaining
  text without submitting anything; press it again to send a buffered command.
- **1 / 2** select a response when numbered choices are displayed.
- `COMMS` repeats the current message and available choices.
- `WAIT` advances to the next scheduled contact. You never need to wait hours
  in real life. Scheduled tasks also run in real time, including while the app
  is closed. A live panel shows local time, completion ETA, remaining time,
  and a segmented progress bar. Scans take eight seconds; `WAIT` skips them.
  Automatic completion stops at the next decision and never chooses for you.
- `SCAN`, `SIGNAL`, `ARCHIVE MERIDIAN`, and `ARCHIVE STATION` uncover evidence.
- `STATUS`, `CREW`, and `MISSION` reflect your current situation.
- `LOG` recalls recorded contacts, discoveries, and decisions. Use **Page Up /
  Page Down** or the mouse wheel to read the transcript.
- **Up / Down** recall commands; **Ctrl+V** pastes one line.
- `HELP` lists commands. `CLEAR` and `REBOOT` refresh the terminal while keeping
  your progress. `EXIT` closes the app.
- `NEW GAME`, followed by `CONFIRM RESTART`, starts over. `CANCEL` keeps the
  current episode.

Reconnaissance, equipment checks, and power decisions change crew condition,
evidence, dialogue, elapsed mission time, and the state you leave the station
in. Story choices use exact commands and numbered responses; this is authored
interactive fiction, not a generative chatbot or a general natural-language
assistant. All people, telemetry, and events are fictional.

## Saved progress

Story progress and the mission log save automatically after discoveries and
decisions to `%LOCALAPPDATA%\MU-TH-UR\meridian-v1.save`. Opening the app resumes
the latest contact and decision, including a message that was still arriving
when you closed it. The transient screen transcript and command history are
not saved; `LOG` retains the significant contacts and decisions.

Saves use a versioned local format and replace the previous checkpoint only
after a temporary file is successfully written. Save failures are reported
in the terminal. An unreadable or unsupported save is preserved; explicitly
confirming `NEW GAME` permits its replacement. Older saves are upgraded on the next save; a pending wait in an older save
starts its timer when first opened because no previous timestamp was stored.
There is one save slot and no
cloud service. Run only one interactive instance at a time.

## Display

The widescreen 16:9 terminal has a dark bottle-green glass background, soft
phosphor glow, and an original olive-green recessed bezel. The molded housing
has rounded corners, a raised outer rim, a deep inner recess, and a broad lower
sill. Reflections sampled from the live screen fall across the inner walls.

The housing and CRT filter are original project code: bowed geometry, softened
scanlines, a subtle phosphor texture, edge shading, and a gentle midtone lift.
The filter runs at final display resolution. No external shaders or graphics
assets are bundled. Appearance is fixed; there are no effect controls.

## Development

`src/main.cpp` implements the Win32 / GDI+ display, input, snapshots, and atomic
Windows save replacement. `src/console.h` contains the platform-independent
story state, authored messages, evidence, and command interpreter. `src/save.h`
encodes and validates checkpoints, including task deadlines. There is no shell execution or networking.

`build.bat` compiles the app and runs both test suites without opening a window.
Story tests exercise 192 complete combinations of decisions, save/load at every
checkpoint, invalid commands, restart confirmation, and damaged saves. Rendering
tests exercise the actual app submission path, disk persistence in a temporary
test directory, scrollback, resizing, and cached CRT frames.

For reproducible off-screen previews:

```sh
build\MU-TH-UR.exe --snapshot
build\MU-TH-UR.exe --snapshot-story
build\MU-TH-UR.exe --snapshot-task
```

These write `preview.png` (the opening), `preview-story.png` (a surface
conversation), and `preview-task.png` (an active scan) in the current directory.
None creates a window or reads or writes the player's save. Rendering tests
also leave the player's save alone.

The footer displays `INTERFACE VER.` followed by the version embedded at build
time. Both build methods use the nearest reachable version tag; source downloads
without Git metadata use the project version in `CMakeLists.txt`. Rebuild after
tagging to update the displayed version. The app itself does not need Git.

Unofficial fan-inspired project. No movie artwork, audio, fonts, or dialogue are bundled.

## Contributing

Contributions are welcome! Bug reports, documentation improvements,
story improvements, and fixes all help make MU/TH/UR better.

To report a bug or suggest an improvement, [open an issue](https://github.com/SoniRomanov/MU-TH-UR/issues).
For bugs, include steps to reproduce, the app version, and what you expected
to happen. For larger changes, start with an issue to discuss the idea.

To contribute code or documentation:

1. Fork the repository, clone your fork, and create a branch for your change.
2. Make a focused change and update relevant documentation. For behavior
   changes, add or update tests that cover the result.
3. Run `build.bat` to compile and run the story, save, and off-screen
   rendering tests. For visual changes, also run
   `build\MU-TH-UR.exe --snapshot`, `--snapshot-story`, and `--snapshot-task`,
   then inspect all three previews.
   Documentation-only changes do not need a build.
4. Push your branch to your fork and open a pull request against `main` in
   this repository. Explain what changed, why, and how you verified it;
   link any related issue. The maintainer reviews changes before merging.

Keep the app offline and use original code and examples. Do not introduce
external models, datasets, libraries, copied shaders, or movie assets/dialogue.
Leave generated builds and local assistant metadata out of pull requests.

By submitting a contribution, you agree to license it under this project's
[MIT License](LICENSE). Submit only work you have the right to license this
way; you retain copyright in your contributions.

## License

Copyright (C) 2026 Soni Romanov.

MU/TH/UR is licensed under the **MIT License** (`MIT`).
See [LICENSE](LICENSE) for the complete terms.
Enter `LICENSE` in the app for copyright, warranty, and redistribution information.

You may use, modify, and redistribute this software, including commercially.
Include the copyright and permission notice in all copies or substantial
portions of the software.

This software comes with **no warranty**, to the extent permitted by law.
The license covers this project's code; it grants no rights to third-party
movie branding or assets.

GitHub build downloads include the MIT license and a source archive from the
same commit as the executable.
