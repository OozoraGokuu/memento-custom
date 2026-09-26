# Memento Custom 2.3.1

Windows x86_64 installer and portable ZIP, Linux x86_64 Flatpak, and macOS Apple Silicon / Intel packages.

- Selecting a Jimaku or Kitsunekko title remembers that provider and title for the playing Media Library entry, across all its episodes. Saved links survive restarts; old libraries remain compatible.
- Subtitle search selects the saved provider and offers Browse episode subtitles, Change link, and Unlink. Linked browsing requests the saved title directly without repeating a catalog search. Explicit selection replaces the link; automatic fuzzy matches never create links.
- Previous/Next subtitle buttons use the complete parsed primary subtitle timeline and apply subtitle delay once. Previous restarts the latest cue after 0.4 seconds, or moves to the preceding cue near its start. Quick double presses still seek three seconds. Unsupported or incomplete timelines retain mpv navigation.
- Regression coverage includes library persistence and isolation, provider selection/direct access, deleted remote entries, timing boundaries and delay, and switching to downloaded subtitles while preserving secondary subtitles.

Existing subtitle downloads, ZIP handling, auto-fetch, AniList, media libraries, and torrent playback remain available. OCR remains disabled. Packages retain their existing signing limitations.
