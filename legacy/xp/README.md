# Experimental Windows XP port

This is an **incomplete prototype**, not Memento Study Edition with feature parity.
It is isolated from the Qt 6 application and uses a native Win32 interface and
VideoLAN's libVLC 3.0.21 playback engine. Target: Windows XP SP3, 32-bit.

Implemented so far:

- Local media playback, pause and seeking with software video output.
- UTF-8 SRT sidecars, including multiline Japanese and Arabic text. A matching
  `.srt` alongside the video loads automatically; other files use **Load SRT**.
- **Ctrl+C** copies the complete active subtitle without opening a subtitle list.
- **Ctrl+L** opens a separate recent-media library window. Double-click to play.
- Selecting an audio track saves its ordinal for later episodes and sessions.
  An unavailable ordinal remains saved for a subsequent episode that has it.
- Settings are separate, under `%APPDATA%\MementoXPPrototype`.

Not ported: dictionary/Shift lookup, Anki, Migaku, OCR, subtitle providers and
next-episode fetching, regex cleanup, embedded subtitle text extraction, ASS
parsing, torrent search, AniList, poster metadata, media export, and the current
application's full UI. Embedded subtitles can play through VLC but cannot yet be
copied; copying currently requires an external UTF-8 SRT.

## Current result

The XP SP3 guest passed 15/15 prototype checks; see
[the recorded result](validation/xp-sp3-prototype.txt) for the exact binary hash.
The player window also rendered a video frame and Japanese/Arabic subtitles.
Continuous GUI playback and audible output still need verification. The complete
modern Memento feature set and twelve original suites remain unported.

## Build

Use the **Win32 threading model of MinGW GCC 10**, such as Ubuntu 22.04's
`g++-mingw-w64-i686-win32`. GCC 13's Win32 C++ runtime imports Vista condition
variable functions even when application code targets XP. It is not suitable.

VLC 3.0.24 was rejected after actual XP loading exposed post-XP dependencies in optional plugins. This prototype pins 3.0.21 and suppresses optional-plugin loader dialogs during initialization.

Extract the official VideoLAN `vlc-3.0.21-win32.7z` archive. SHA-256:
`77b2a79c4baf0dcc7c453f74f29fbdb55d42c790d277b5f9e0dbed0f3abc0131`.

```sh
cmake -S legacy/xp -B build-xp \
  -DCMAKE_TOOLCHAIN_FILE=mingw32.cmake -DCMAKE_BUILD_TYPE=Release \
  -DVLC_SDK=/absolute/path/to/vlc-3.0.21/sdk
cmake --build build-xp --parallel
python3 legacy/xp/package.py --vlc /absolute/path/to/vlc-3.0.21 \
  --build build-xp --output dist/Memento-XP-Prototype
```

Extract the entire portable package into a folder of your choice, then run
`memento-xp-prototype.exe`. The DLLs and `plugins` directory must stay alongside it.
No existing Memento installation or preferences are replaced.

## Validation

The nine portable core checks run on Linux using CMake/CTest. The XP executable's
`--self-test <two-audio-track-fixture.mkv>` mode additionally checks Unicode
conversion, the real Windows clipboard, persisted audio preferences, libVLC
initialization, decoding/time progression, and selecting the second audio track.
It exits nonzero on failure and writes `%TEMP%\memento-xp-tests.txt`.

These are prototype checks. **The original twelve Qt application test suites
have not been ported or run on XP.** GUI rendering, real audio output, shortcut
behavior, and cross-episode preferences also need interactive validation.

GitHub's XP workflow cross-compiles and tests the portable core; it does not
claim to run Windows XP. Guest results must identify the tested executable's
SHA-256 and be collected on the actual XP VM.

VLC runtime license and attribution are included in the package. Its source is
available from https://download.videolan.org/pub/videolan/vlc/3.0.21/ and
https://code.videolan.org/videolan/vlc/-/tree/3.0.21 . This prototype's source and
the repository GPLv2 license accompany the package.
