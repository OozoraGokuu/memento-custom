# Memento Custom 2.3.3

Fixes AniList progress syncing for filenames such as `Slam Dunk 49 Takezono, Last Fight.mkv`.

- Uses neighboring filenames in the same library folder or torrent to recognize a consistent numbered episode series. Explicit `S01E49`, `EP49`, and ` - 49` formats retain priority.
- Avoids guessing when numeric fields are ambiguous, are metadata, or exceed the linked season’s episode count after applying its offset.
- Saves manual episode corrections across restarts and changing torrent playback URLs. Corrections are scoped to the file and linked season/offset. **Detect automatically** clears a correction.
- Invalidates an older queued update when its episode number is corrected, preventing an outstanding read from submitting the previous number.
- Shows an explanation in AniList settings when the episode needs confirmation.

Install the new package over the existing app. Accounts, dictionaries, title links and local progress are preserved. Previously missed episodes are not automatically backfilled; confirm the playing episode and use **Sync now**, or reach the configured playback threshold. Existing AniList progress is never reduced.

Downloads: Windows installer or portable ZIP; Linux Flatpak; macOS ZIP for Apple Silicon (`arm64`) or Intel (`x86_64`, macOS 15+). Windows packages are unsigned; macOS packages are ad-hoc signed and are not Apple-notarized. OCR remains disabled.

Packages are built from clean source and audited for personal profiles and credentials. Tests use synthetic accounts; physical hardware and live account writes are not covered. See [validation coverage](https://github.com/OozoraGokuu/memento-custom/blob/v2.3.3/docs/VALIDATION.md).
