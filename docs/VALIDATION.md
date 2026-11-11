# Validation coverage

The Windows CI job compiles native x86_64 binaries with warnings treated as errors and runs the same CTest suite as Linux. It verifies runtime dependency closure, startup of the packaged application, and installer behavior (installation, upgrades, rollback, running-process protection, preservation of unrelated files, and uninstall). Linux also builds and starts the Flatpak package.

| Feature | Automated coverage |
| --- | --- |
| Subtitle parsing, timing, dual tracks | Parser/model and bitmap-codec tests |
| Auto-pause | Player-manager state transition tests |
| Dictionaries | Synthetic Yomitan import and lookup, queued imports, duplicate-default detection, local HTTP download/import, cancellation and invalid archives; Japanese/Arabic paths. Real Jitendex/JPDB archives additionally imported and queried on Linux. |
| AnkiConnect | Real local HTTP connection, API key, deck/field lookup, Japanese note submission and server errors against a fake Anki service |
| Anki sentence video | Real optional video note payload, five-second timing and file-boundary guards; portable-package H.264/AAC MP4 creation, fast-start metadata, target-field validation, Unicode paths, and five seconds of decoded, non-silent audio |
| Jimaku | Matching and archive handling with synthetic fixtures |
| Saved subtitle release | Independent episode/format matching, paired season/absolute episode numbers, source-service and variant boundaries, legacy saved rules, current-series targeting, repeated fetches, numbering offsets, mixed ASS/SRT files, ambiguous candidates, and refreshed Kitsunekko catalogs |
| AJATT mirror | Catalog/cache/download logic tests |
| Torrent RSS search | User-configured URLs, same-origin RSS discovery and torrent links, request parameter preservation, RSS parsing, sorting and DNS fallback transport tests |
| Torrent streaming | Engine selection, HTTP byte-range streaming of synthetic data, invalid ranges, normal cleanup and link/junction safety tests |
| AniList | Authorization callback, explicit/contextual episode detection, ambiguous and metadata-number rejection, season bounds, persistent per-file corrections, progress rules, queue, stale account and retry tests using a fake service |
| Migaku export | Actual video/subtitle playback, separate JPG/MP3 capture, duplicate capture, MP3 decoding and non-silent PCM signal; clip guards and rollback |
| Privacy | Source/package scans; isolated fresh-profile account defaults |
| Windows portability | Fresh runner without MSYS2; Japanese/Arabic install, media and export paths; MeCab lookup; verified HTTPS with bundled certificates |
| Auto clipboard | Actual subtitle-to-clipboard capture during the media check |
| Settings UI and activity | Instantiate all Options pages; verify subtitle loading shows and clears the Activity panel |

Automated tests cannot establish that every UI interaction, GPU/audio driver, or external account works. Live AniList writes, authenticated Jimaku downloads, Anki card creation and Migaku Memory import require a user's own accounts and must be checked separately. CI does not use or publish the maintainer's credentials. Public torrent peers and provider availability are not deterministic test services.

Before relying on a new machine, play a local file, verify picture and sound, load primary/secondary subtitles, try lookup and auto-pause, export one image/audio pair, and confirm its audio plays. Configure and test accounts individually; check episode mapping before enabling automatic AniList updates.

## Real-file subtitle regression check

The paired-number matching fix was also checked on Linux with existing Oshi no Ko season 3 video files, episodes 4 through 8, and their Amazon Japanese SDH SRT releases. The old installed build and the previous preview both reproduced the failure: episode 4 restored its saved subtitle, but episode 5 did not select an external subtitle. Those filenames contain both a season-relative number (`S03E05`) and an absolute number (`第29話`).

With the fix, an isolated profile initially containing only the episode 4 subtitle choice selected the corresponding release for every episode through episode 8. Both file-load transitions and clicks on the Next episode button were checked. Each selected external filename, visible subtitle text, and advancing playback position was verified; episode 8 also exercised downloading from the live provider. Automated regression cases additionally reject a different source, format, season, language, SDH variant, or absolute numbering offset, and cover existing saved v1/v2 rules.

The videos, subtitle contents, screenshots, account settings, and library profile are not included in the repository. These checks used a separate profile and silent audio output; they do not validate audio hardware or establish that an entire episode was watched without interruption.

## macOS

The macOS workflow tests Apple Silicon and Intel separately on macOS 15 runners on pushes, pull requests and manual runs. Publication requires CTest, source/package privacy audits, extracted-ZIP startup, complete bundled library checks, architecture and deployment-target checks, ad-hoc signature verification, and real subtitle/image/audio/Anki-video export to pass. The extracted package media test removes Qt and dynamic-library search variables and uses a system-only executable PATH. It requires the embedded subtitle timeline to be completely indexed using the app's bundled FFmpeg helper, whose dependencies, architecture, signature, license declaration and build options are checked with the app. Anki video encoding and audio decoding use the deployed libmpv runtime.

This does not establish compatibility with every physical Mac, display, audio device, or live account. Gatekeeper notarization is not provided by these ad-hoc signed builds.
