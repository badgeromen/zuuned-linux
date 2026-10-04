# Prepared audio metadata

Run `node tests/audio-disc-tags/run.mjs`, then
`SANITIZE=1 node tests/audio-disc-tags/run.mjs` for ASan/UBSan.

The 197 checks use generated media and the real FFmpeg/LAME preparation code.
They cover FLAC/Ogg/MP3 disc tags, container versus stream metadata precedence,
missing/invalid values, explicit disc overrides, and unchanged decoded PCM
after MP3 retagging. Temporary media stays outside the user's library.

The original C entry points remain available; additive `_with_disc` entry
points accept a positive override or preserve the source when zero is supplied.
No wire properties or device readback guarantees are inferred from these tests.
