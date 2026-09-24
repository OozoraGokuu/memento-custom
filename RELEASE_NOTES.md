# Memento Custom 2.3.0

Windows x86_64 installer and portable ZIP, plus a Linux x86_64 Flatpak.

- New Activity panel shows episode loading/buffering, subtitle retrieval/parsing, dictionary setup, torrent search and sentence-media export.
- Torrent search now starts blank: enter your own website or compatible torrent RSS feed URL. Websites can advertise their RSS feed for automatic discovery. No provider domain, preset mirror or branded provider button remains. Seeder sorting and Cloudflare DNS with system fallback remain available.
- Fresh profiles automatically download Jitendex Japanese–English definitions and JPDB word frequencies. Options → Dictionaries provides install/retry, progress and cancellation. Existing dictionaries are preserved.
- Dictionary imports run sequentially; queued imports can be cancelled without interrupting an active database import.

Existing Jimaku/AJATT subtitle search, media library, direct torrent playback, dual subtitles, auto-clipboard, personal AniList authorization, Anki and Migaku JPG/MP3 folder export remain available. OCR is disabled.

No personal accounts, tokens, API keys, history, dictionaries, screenshots or audio are bundled. Default dictionaries are fetched from their public sources after startup. MeCab IPADIC remains the standard bundled upstream language resource.

Windows packages are unsigned. Validation coverage and account/hardware testing limits are documented in docs/VALIDATION.md.
