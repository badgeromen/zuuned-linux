# Custom photo albums

Photos has two views: **Folders** retains the existing source-directory browser;
**Albums** holds independently named collections and nested subalbums. Creating,
renaming, moving, deleting and changing album membership works without a Zune.
Source files stay in their original locations.

Choose **Albums → New album** to open the floating photo builder beside the
sidebar, following the mixtape builder's browse/collect/arrange/save flow.
**Edit album** opens an existing draft; **New subalbum** defaults its parent to
the album being browsed. Browse source folders while the builder stays open.
Add individual photos, drag a folder or selected photos into the island, or use
Select mode, Ctrl-click toggles, Shift-click ranges and Select All for large
batches. Selections are retained across folders until cleared.

The builder owns a separate draft from the music mixtape. Its name, parent and
ordered photo membership are staged until **Save**. **Cancel** and replacing a
dirty draft require discard confirmation. Save commits all fields together;
errors preserve the draft. Existing-album saves compare the original name,
parent and ordered membership to the current database, rejecting a stale edit
instead of overwriting another change. Reopen the changed album before retrying.
The browser and builder use virtualized photo views and ID-based selection;
bulk selection does not instantiate a visual or decode an image for every photo.

An album may contain both photos and subalbums. The same photo can belong to
several albums, once per album. Membership is ordered; Move earlier/later changes
the gallery and transfer selection order. Removing a photo from an album removes
only that membership. Removing it from the library removes every membership but
does not delete its source file. Deleting an album requires confirmation and
deletes its subalbums and memberships, preserving the photos and source files.

Names are trimmed, limited to 200 characters, and unique within the same parent
under SQLite's NOCASE comparison. The same name may be used under a different
parent. Trees are limited to 32 levels; moving a subtree validates its total
depth and rejects self/descendant destinations. Stable album and photo IDs are
not reused after deletion, so a stale picker cannot attach a replacement photo.

Schema v7 adds `photo_albums` and `photo_album_photos`, with cascading foreign
keys for membership cleanup. Existing photo rows retain their IDs when migrated
to AUTOINCREMENT. The existing SQLite backup path preserves the complete
pre-migration database, including committed WAL data. A v6 library receives
`library.db.v6-backup`. Older app versions must refuse this newer schema.
Album edits use savepoints and report success only after commit. Reordering
requires the exact current membership set; stale reorders cannot silently remove
photos added since the editor read the album.

Scanning an existing photo path updates the same photo row and preserves its
memberships. Removing a watch folder removes indexed photos and their
memberships, while retaining the empty album tree. Re-adding a deleted source
does not reconstruct removed memberships. Renaming files outside the app is not
a supported automatic membership-relink operation.

Queueing an album selects **only its direct photos**, with that album's name.
Subalbums are separate selections. This release does not claim that the Zune
mirrors the local nested tree. Existing filename-based device-photo dedup and
badges remain unchanged: sharing a photo among local albums does not establish
multiple on-device copies or memberships. Local photo drags can land in the
album builder offline (approved September 27). A separate local drag key keeps
them distinct from device transfers. Device-queue actions and drops still
require a connected Zune and recheck at completion; the music/video/device-photo
drag rules remain unchanged.

Verification lives in `tests/photo-albums/` and `tests/actions/photo-run.sh`.
Those isolated gates do not touch the user's library or device. Packaged photo
sync and physical device presentation remain separate hardware release checks.
