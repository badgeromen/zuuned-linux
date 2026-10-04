# Native artwork request/cache gate

Run from the repository root:

```sh
bash tests/artwork-pipeline/run.sh
```

The gate compiles the real `ArtworkRequest`, `ArtworkPipeline` and unchanged
`PrintRenderer` into an isolated executable. Synthetic images and all caches
live in a temporary directory. It opens no real library, device, network client
or GUI and requires only the app's existing Qt libraries.

Coverage includes source-byte/alpha preservation, canonical 800px renderer
parity, query-suffixed local URLs, style/control changes, unsupported URL
pass-through, same-path replacement and repair, interrupted source snapshots,
duplicate URL/content coalescing, bounded two-worker admission and overflow
retry, destroyed subscribers, persistent/decoded cache reuse, corrupt-cache
repair, cache errors, oversized sources and source symlink safety. A controllable
renderer holds work in flight to exercise actual cancellation and stale-result
handling; output-pixel checks use the production renderer.

Display lease tests acknowledge PNGs with `displayed(url)` and force eviction
pressure before/after the asynchronous replacement is acknowledged. Both the
visible image and latest loading image stay protected. Source changes, original
mode and errors release those display leases. Source URLs themselves stay
protected while referenced, including aliases to an owned cache image. Cache
symlinks are rejected; cached reads never modify source or target timestamps.

Production defaults are two workers, 96 pending distinct jobs, 64 MiB of decoded
images and at most 512 idle PNGs / 256 MiB on disk. Active source/display leases
are excluded from disk eviction; a debounced worker pass restores idle limits
when they are released. Overflow subscribers retry while busy and disappear
cleanly when a view is destroyed. File reads, decoding, PNG validation, atomic
writes and pruning happen on workers. Requests perform no filesystem stat calls
on the UI thread.

The source snapshot is bounded to 32 MiB encoded / 64 million declared pixels;
canonical decoded artwork has an 800px maximum long edge. A content hash and
renderer/decode revision identify each variant; Halftone-only controls do not
invalidate other styles. Explicit `refresh()` rereads content even when a path
and timestamp have stayed the same. This is a display-only cache: original URLs
remain the source of metadata edits and device transfers.
