# Memento Custom 2.3.4

This release updates playback controls and the study workflow.

- Rewind and forward buttons now seek exactly 5 seconds relative to the current position, like mpv's default left and right keys. Repeated presses continue in the chosen direction.
- Adds an immersion statistics tab and an AniList search dialog with episode matching and offset controls.
- Fixes selection from the media-library episode list.
- Adds **Subtitle → Include Subtitles in Stream**. On X11, turning it off keeps text subtitles visible locally while a capture of the Memento application window omits them. Screen sharing and bitmap subtitles cannot be separated this way.

The Windows package is built and checked by GitHub Actions on a Windows runner. It contains no user profile, account, API key, or watch-history files. Install it over an existing version to retain your local settings. The Windows build is unsigned.
