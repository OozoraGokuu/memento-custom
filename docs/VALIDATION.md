# Validation coverage

The Windows CI job compiles native x86_64 binaries with warnings treated as errors and runs the same CTest suite as Linux. It verifies runtime dependency closure, startup of the packaged application, and installer behavior (installation, upgrades, rollback, running-process protection, preservation of unrelated files, and uninstall). Linux also builds and starts the Flatpak package.

| Feature | Automated coverage |
| --- | --- |
| Subtitle parsing, timing, dual tracks | Parser/model and bitmap-codec tests |
| Auto-pause | Player-manager state transition tests |
| Dictionaries | Synthetic Yomitan import and lookup, queued imports, duplicate-default detection, local HTTP download/import, cancellation and invalid archives; Japanese/Arabic paths. Real Jitendex/JPDB archives additionally imported and queried on Linux. |
| AnkiConnect | Real local HTTP connection, API key, deck/field lookup, Japanese note submission and server errors against a fake Anki service |
| Jimaku | Matching and archive handling with synthetic fixtures |
| AJATT mirror | Catalog/cache/download logic tests |
| Torrent RSS search | User-configured URLs, same-origin RSS discovery and torrent links, request parameter preservation, RSS parsing, sorting and DNS fallback transport tests |
| Torrent streaming | Engine selection, HTTP byte-range streaming of synthetic data, invalid ranges, normal cleanup and link/junction safety tests |
| AniList | Authorization callback, progress rules, queue, stale account and retry tests using a fake service |
| Migaku export | Actual video/subtitle playback, separate JPG/MP3 capture, duplicate capture, MP3 decoding and non-silent PCM signal; clip guards and rollback |
| Privacy | Source/package scans; isolated fresh-profile account defaults |
| Windows portability | Fresh runner without MSYS2; Japanese/Arabic install, media and export paths; MeCab lookup; verified HTTPS with bundled certificates |
| Auto clipboard | Actual subtitle-to-clipboard capture during the media check |
| Settings UI and activity | Instantiate all Options pages; verify subtitle loading shows and clears the Activity panel |

Automated tests cannot establish that every UI interaction, GPU/audio driver, or external account works. Live AniList writes, authenticated Jimaku downloads, Anki card creation and Migaku Memory import require a user's own accounts and must be checked separately. CI does not use or publish the maintainer's credentials. Public torrent peers and provider availability are not deterministic test services.

Before relying on a new machine, play a local file, verify picture and sound, load primary/secondary subtitles, try lookup and auto-pause, export one image/audio pair, and confirm its audio plays. Configure and test accounts individually; check episode mapping before enabling automatic AniList updates.
