# Artwork state and scanner metadata

Run `bash tests/artwork-persistence/run.sh` to exercise real SQLite collection
customizations and the automatic track upsert used by scanning. The fixture
uses disposable databases and no provider or media-file access.

Seven checks ensure `_artworkDiscovery` and `_artworkMatch` do not become
metadata pins merely because they are persisted in `identity_json`. Existing
manual identity, custom artwork and `user_edited` protections remain intact.
Saved artwork state itself is preserved during re-probing.
