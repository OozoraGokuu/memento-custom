

Vibecoded, added lots of QoL changes that i believed should've been in memento.
# Memento Custom

An unofficial Windows and Linux build of [Memento](https://github.com/ripose-jp/Memento), an mpv-based Japanese study player. GPL-2.0; original project and dependency attribution are retained.


[Downloads](https://github.com/OozoraGokuu/memento-custom/releases) · [Build and test results](https://github.com/OozoraGokuu/memento-custom/actions) · [Validation coverage](docs/VALIDATION.md)

## Install

**Windows 10 (1903+) / 11 x86_64:** download `Memento_Windows_x86_64_Installer.exe`, or extract the entire `Memento_Windows_x86_64.zip` and run `memento.exe`. Keep its DLL, QML, dictionary, certificate and license files together. The installer uses the name **Memento Study Edition**. Windows builds are unsigned. CI runs on Windows Server 2025; desktop Windows 11 hardware is not part of the automated test environment.

**Linux x86_64:** install Flatpak, then run:

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./Memento_Linux_x86_64.flatpak
flatpak run io.github.mu0dev.MementoCustom
```

This Flatpak has its own app ID, separate from upstream Memento. Export folders outside your home directory may require a Flatpak filesystem permission. Upstream Flathub/AUR builds do not include this fork's additions.

## Included features

- mpv playback, track selection, seeking, playlists, screenshots, configurable shortcuts and mpv settings.
- Activity panel for episode loading/buffering, subtitles, dictionary setup, torrent searches and media exports.
- Default **Jitendex** definitions and **JPDB** word-frequency dictionaries download automatically on a fresh profile. Install/retry them from Options → Dictionaries; existing dictionaries are preserved.
- Japanese dictionary lookup, MeCab inflection analysis, Yomitan-format dictionaries and kanji lookup. [Dictionary downloads](docs/DICTIONARIES.md).
- AnkiConnect card creation with configurable fields and image/audio/video media.
- Primary and secondary subtitle rendering, subtitle lists and navigation, timing adjustment and auto-pause.
- Persistent media library for local folders and torrents, episode progress and watched state.
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
- **AniList:** create your own application in AniList Developer Settings. Set its redirect URL to `http://127.0.0.1:47832/callback`, enter the Client ID in Options → AniList Integration, then connect. No client secret is needed. Link each library title and check its episode numbering before enabling automatic progress updates.
- **Migaku:** Options → Migaku Integration → enable integration and choose an output folder. Pause on a subtitle and press **Ctrl+Shift+M**, or use the export action. Add the resulting JPG under Images and MP3 under Sentence audio in Migaku's Card Creator.
- **Anki:** install AnkiConnect in your own Anki profile, then configure Memento's Anki options.

## Building

The GitHub Actions workflow is the complete build recipe. Windows uses MSYS2 UCRT64 and `windows/build-installer.sh`; Linux uses Qt 6.9 or later and the dependency manifest at `linux/flatpak/io.github.mu0dev.MementoCustom.json`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMEMENTO_MECAB_SUPPORT=ON -DMEMENTO_OCR_SUPPORT=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

See [validation coverage](docs/VALIDATION.md) for what CI verifies and what needs testing with your own accounts. Releases contain source from this repository, never a copy of a user's installed profile.

Windows bundles dependency licenses and a `package-versions.txt` inventory. Dependency build recipes are in [MSYS2 MINGW-packages](https://github.com/msys2/MINGW-packages); matching source archives are available from the [MSYS2 source mirror](https://repo.msys2.org/mingw/sources/). Linux dependency sources and patches are recorded in the Flatpak manifest. Memento's source is available at each release tag in this repository.
