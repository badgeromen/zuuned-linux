# Private and public authentication builds

October 3, 2026: `mtpz.c` includes `mtpz_keys.h` only when available.
The private repository retains that header and its existing embedded fallback.
A public source export omits the header and all private Git history.

Both builds first try the existing local `~/.mtpz-data` loader. A public build
without that file reports unavailable credentials and leaves
`mtpz_keys_available()` false. No authentication constants or replacement
credentials are generated. An embedded-data build still contains that data
in its compiled output, regardless of where its source is hosted.

Linux software verification checked both missing-file paths: public builds
return an error with no keys; private builds retain embedded availability.
This did not perform a physical Zune authentication test.
