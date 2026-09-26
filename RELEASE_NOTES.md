# Memento Custom 2.3.2

- Dictionary lookup defaults to holding Shift.
- Ctrl+C copies the complete current primary subtitle directly from the player, with no subtitle list required.
- Subtitle cleanup uses the same Unicode-aware regular expressions for on-screen text, list text, copying, and list search; invalid patterns report an error instead of silently differing between views.
- The subtitle menu can fetch the next episode from the last successful Jimaku/Kitsunekko search, using the same title directly.
- Media Library opens in its own window with searchable collection cards and title/unfinished filters.
- Manually chosen audio-track numbers persist per library entry and are restored for subsequent episodes when available.
- The interactive Windows installer now offers an installation-directory page. Flatpak uses its standard user/system installation locations.

Windows, Linux, and macOS use the same shared implementation.
