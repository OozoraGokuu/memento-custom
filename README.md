

Vibecoded, added lots of QoL changes that i believed should've been in memento.
# Memento Custom

An unofficial fork of [Memento](https://github.com/ripose-jp/Memento), an mpv-based Japanese study player, with Windows, Linux and macOS builds. GPL-2.0; original project and dependency attribution are retained.


[Downloads](https://github.com/OozoraGokuu/memento-custom/releases) · [Build and test results](https://github.com/OozoraGokuu/memento-custom/actions) · [Validation coverage](docs/VALIDATION.md)

Earlier packages remain in the [previous repository's release archive](https://github.com/OozoraGokuu/memento-custom-archive/releases). New work is published in this fork; updating source on GitHub does not update an existing installation.

## Install

**Windows 10 (1903+) / 11 x86_64:** download `Memento_Windows_x86_64_Installer.exe`, or extract the entire `Memento_Windows_x86_64.zip` and run `memento.exe`. Keep its DLL, QML, dictionary, certificate and license files together. The installer uses the name **Memento Study Edition**. Windows builds are unsigned. CI runs on Windows Server 2025; desktop Windows 11 hardware is not part of the automated test environment.

**Linux x86_64:** install Flatpak and download `install-memento.sh` and `Memento_Linux_x86_64.flatpak` into the same folder. The helper shows the user and system installation locations and lets you choose:

```sh
bash ./install-memento.sh
flatpak run io.github.mu0dev.MementoCustom
```

Flatpak manages its standard user (`~/.local/share/flatpak`) and system (`/var/lib/flatpak`) locations; the helper does not install into arbitrary folders. This Flatpak has its own app ID, separate from upstream Memento. Export folders outside your home directory may require a Flatpak filesystem permission. Upstream Flathub/AUR builds do not include this fork's additions.

**macOS 15 or later:** choose `Memento_macOS_arm64.zip` for Apple Silicon (M1 or newer), or `Memento_macOS_x86_64.zip` for Intel. Extract the ZIP and drag **Memento.app** into Applications. No Homebrew or MacPorts installation is needed. These builds use an ad-hoc signature and are not Apple-notarized; macOS may require approval in System Settings → Privacy & Security after the first launch attempt.

## Included features

- mpv playback, track selection, seeking, playlists, screenshots, configurable shortcuts and mpv settings.
- Activity panel for episode loading/buffering, subtitles, dictionary setup, torrent searches and media exports.
- Default **Jitendex** definitions and **JPDB** word-frequency dictionaries download automatically on a fresh profile. Install/retry them from Options → Dictionaries; existing dictionaries are preserved.
- Japanese dictionary lookup, MeCab inflection analysis, Yomitan-format dictionaries and kanji lookup. [Dictionary downloads](docs/DICTIONARIES.md).
- AnkiConnect card creation with configurable fields and image/audio/video media.
- **Add video** when adding a word to Anki attaches a five-second MP4 from that word's subtitle sentence to a field you choose. It starts at the sentence's playback time; near the end of a file, it shifts back to keep five seconds, and files shorter than five seconds use their available length. Clips include the selected audio track and use H.264/AAC for playback on Windows, Linux and macOS. The ordinary Add word action keeps your existing field and media settings.
- Primary and secondary subtitle rendering, subtitle lists and navigation, timing adjustment and auto-pause.
- Rewind and forward buttons step through the ordered primary subtitle lines, accounting for subtitle delay. The visible counter starts at 1 on the first cue, advances during playback, and resynchronizes after seeking. Buttons wrap at either end. Embedded text tracks are indexed when FFmpeg is available.
- **Ctrl+Z** saves a subtitle screenshot as a PNG in **Pictures/Memento/<library title>/** with the episode name and playback time. Videos outside the library use **Unsorted**. Text fields retain their normal Undo shortcut.
- **Export Video → Export selected section…** saves an MP4 clip to the same **Pictures/Memento/<library title>/** folder as screenshots. Set start/end times (or use the current playback time), choose Original/1080p/720p/480p resolution and Compact/Balanced/High quality, and see the approximate size in MB before exporting. Choose primary and secondary subtitle tracks independently, or None for both; selected subtitles are burned into the video with their playback delays. Audio is optional. **Preview section** renders a temporary clip inside the dialog using the same export settings, with play/pause, seeking and a separate mute control. Previews start muted and clear when the settings change or the dialog closes. Export runs in the background with progress and cancellation; it does not change playback. Resolution never upscales the source. Size is an estimate, not a guaranteed file size.
- **Media → Add Multiple Magnets…** (also in the library’s **+ Add** menu) combines 2–100 distinct magnets into one named entry. Paste one link per line. Existing individual entries merge with their watched progress; episodes retain their own torrent source and appear as metadata arrives.
- **Statistics → Immersion calendar** shows a GitHub-style yearly heatmap of active watch time. Choose a year, hover a square for its duration, or select a day to see titles watched and episode completions. It uses your existing local playback history.
- **Statistics → Dictionary lookup history** saves successful lookups with their reading and source text. Subtitle lookups also include the series, episode and playback position. Search your history, filter by date range, reopen a definition, or jump back to a local video scene. You can remove individual entries or clear the history. Lookups start being recorded with this update and stay on your device.
- AniList title search and episode matching, and a media-library episode picker.
- On X11, **Subtitle → Include Subtitles in Stream** controls whether text subtitles appear in a capture of Memento's window while they remain visible locally. Screen sharing and bitmap subtitle tracks cannot separate them.
- Persistent media library for local folders and torrents, episode progress and watched state.
- **Remembered subtitle releases:** choose a subtitle once from Jimaku, Kitsunekko, or **Subtitle → Add Subtitle…**. Each library episode remembers its exact file and timing offset. Later episodes match the linked source/group and the same file format independently of filename spacing and episode titles: choosing `EP 1.ASS` can load `EP2.ASS`, while ignoring `EP2.SRT`. Explicit season, language and subtitle variants are preserved; local files stay in the chosen folder. **Subtitle → Fetch saved subtitle for current episode** retries the playing episode using its saved numbering offset; repeated clicks never advance an episode counter or reuse another series' search. Missing or ambiguous matches ask you to choose. Subtitle Search shows the saved release, an auto-load switch, and Unlink. Choices stay in your local library data.
- Japanese subtitle search through **Jimaku** (your API key) and the online **AJATT Kitsunekko mirror** (no full mirror download). No OpenSubtitles integration.
- Torrent RSS search using a **website/feed URL you supply**, seeder/newest sorting, magnet/torrent import and direct streaming of one selected episode. No search provider or mirror is preconfigured.
- Optional Cloudflare DNS-over-HTTPS for torrent search, with system-DNS fallback.
- Optional automatic copying of the current primary subtitle to the clipboard.
- Native **AniList** authorization and progress sync using each user's own AniList integration.
- **Migaku media export**: save a screenshot JPG and sentence MP3, named by episode and timestamp, in a chosen folder. Import them manually into Migaku Memory's Card Creator. No browser bridge or Migaku login is required.

OCR is not enabled in these packages, matching the custom build. Provider availability and account permissions depend on the external services.

## Set up your accounts

Every new installation starts without accounts, API keys, watch history, personal dictionaries or export folders. The portable Windows build still stores settings in the current user's profile; it does not make an existing user's configuration disappear.

- **Torrent search:** open Find anime streams and enter your website URL or its compatible RSS feed URL. Memento follows an advertised RSS link on the same website; it does not scrape HTML search results. The feed must provide torrent links/enclosures on the same origin; searches append `q` and preserve any other parameters you put in the URL. No website is contacted until you configure one and search.
- **Jimaku:** enter your own key in Options → Jimaku Subtitles.
- **AniList:** create your own application in AniList Developer Settings. Set its redirect URL to `http://127.0.0.1:47832/callback`, enter the Client ID in Options → AniList Integration, then connect. No client secret is needed. Link each library title and check its episode numbering before enabling automatic progress updates. Plain numbered filenames are detected using neighboring episodes in the same folder or torrent. Ambiguous or out-of-range numbers require confirmation. Manual episode corrections are saved per file and linked season; use Detect automatically to clear a correction.
- **Migaku:** Options → Migaku Integration → enable integration and choose an output folder. Pause on a subtitle and press **Ctrl+Shift+M**, or use the export action. Add the resulting JPG under Images and MP3 under Sentence audio in Migaku's Card Creator.
- **Anki:** install AnkiConnect in your own Anki profile, then configure Memento's Anki options. For **Add video**, choose a field included in your Anki card template (for example, a `Video` field displayed with `{{Video}}`). AnkiConnect stores the MP4 in Anki's media collection; Memento's bundled encoder creates it without a separate FFmpeg installation.

## Building

The GitHub Actions workflow is the complete build recipe. Windows uses MSYS2 UCRT64 and `windows/build-installer.sh`; Linux uses Qt 6.9 or later and the dependency manifest at `linux/flatpak/io.github.mu0dev.MementoCustom.json`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMEMENTO_MECAB_SUPPORT=ON -DMEMENTO_OCR_SUPPORT=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

See [validation coverage](docs/VALIDATION.md) for what CI verifies and what needs testing with your own accounts. Releases contain source from this repository, never a copy of a user's installed profile.

Windows bundles dependency licenses and a `package-versions.txt` inventory. Dependency build recipes are in [MSYS2 MINGW-packages](https://github.com/msys2/MINGW-packages); matching source archives are available from the [MSYS2 source mirror](https://repo.msys2.org/mingw/sources/). Linux dependency sources and patches are recorded in the Flatpak manifest. Memento's source is available at each release tag in this repository.

## macOS builds

The [macOS workflow](.github/workflows/macos.yml) builds each architecture natively with Qt 6.9 and a pinned MacPorts dependency tree. It runs on pushes and pull requests, as well as manually. It runs CTest and checks the extracted app's dependencies, signature, minimum OS version, startup, subtitle capture, clipboard, JPG/MP3 export, and five-second Anki MP4 creation and audio decoding. Dependency notices and the package inventory are included in `Memento.app/Contents/Resources/licenses`. See the workflow result for the tested commit; a running build is not a verified release.
