# Reuse existing artist objects (2026-10-02)

The ZuunedLinux device log at 2026-10-02 04:45 UTC recorded fresh artist
objects for Selena Gomez (134219081) and Zweihänder (134219082), while the user
reported duplicate artists on the Zune HD. The batch map in DeviceWorker only
coalesced names inside one sync; zune_forge_artist unconditionally created a
new 0xB218 artist object on subsequent syncs.

zune_forge_artist now enumerates live AbstractArtist handles and reads their
Name properties before creation. Matching preserves UTF-8 bytes, folds ASCII
case and trims outer ASCII whitespace. A matching handle is returned for normal
album/track linking. Existing duplicates use the smallest matching handle; no
existing object is deleted or relinked by this lookup. Failed enumeration,
unreadable names or cancellation return zero rather than treating failure as
absence. The existing zero-byte commit/orphan cleanup remains.

Gate: `bash tests/run-artist-reuse.sh` exercises the real artist code over an
in-memory MTP boundary with ASan/UBSan: repeat sync, UTF-8 names, existing
duplicates, failed reads, cancellation and commit cleanup. It does not certify
physical device readback or repair existing duplicates. Physical acceptance:
sync another album for an existing artist and verify a reuse log with no new
artist handle or count increase after eject/reindex.

## Live Zune HD evidence (2026-10-02)

Session `zuuned-20261002T051301085Z-540822-36d90b2d-0000.log`
records reuse of Selena Gomez handle 134219071 and Zweihänder handle
134218646 at 05:18 UTC, followed by `forge done: 3 ok, 0 failed`.
Post Malone handle 134219070 was also reused at 05:24 UTC. The three
forge batches reported 3, 1 and 4 successes, each with zero failures.
Final CleanDataStore returned 0 and the USB session closed normally.
This confirms live reuse for those records, not a complete device inventory
or removal of existing duplicates. Jelly Roll was reused at 05:20, then
created at 05:24 after track purges and reconnection; the log does not
establish whether its former artist record survived firmware reindexing.
