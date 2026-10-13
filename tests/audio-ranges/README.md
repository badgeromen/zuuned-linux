# Native audiobook chapter encoding

Run `bash tests/audio-ranges/run.sh` from the repository root. The suite compiles
the real transcode implementation with ASan/UBSan, generates disposable WAV/AAC
M4B fixtures, and exercises `zuuned_transcode_audio_range`. FFmpeg is used only
for fixture creation and independent output decoding, never by the runtime API.

The 34 checks verify adjacent chapter cuts away from packet boundaries, decoded
sample counts, audible content and phase at nonzero offsets, invalid and
out-of-source intervals, rounded final endpoints, refusal of non-regular inputs
and external playlists, and cleanup on preflight/mid-encode cancellation.
Progress must remain monotonic and report completion only after successful file
close. No USB device or user media is accessed.

The generated MP3 includes an Info/LAME frame with encoder delay and padding.
Gapless-aware decoders recover the exact intended PCM duration. A device's
chapter presentation, transitions, artwork and resume still require a separate
hardware test.
