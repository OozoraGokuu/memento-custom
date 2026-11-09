# Synthetic media fixture

`sentence.mkv` contains eight seconds of generated test pattern and a 440 Hz
tone, plus the synthetic Japanese cue from `sentence.srt`. It contains no user
media or account data. The package verification uses it to check playback,
subtitle capture, image/audio export and a five-second Anki video attachment.

Regenerate it from the repository root with:

```sh
ffmpeg -hide_banner -v error \
  -f lavfi -i testsrc2=size=320x180:rate=24:duration=8 \
  -f lavfi -i sine=frequency=440:duration=8 \
  -i tests/fixtures/sentence.srt \
  -map 0:v -map 1:a -map 2:s \
  -c:v mpeg4 -q:v 7 -c:a aac -c:s srt -disposition:s:0 default \
  -y tests/fixtures/sentence.mkv
```
