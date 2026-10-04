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
key loading returns an error with no keys; private builds retain embedded availability.
This is a key-loader result: zune_breach can still return a basic device handle
without successful authentication. Follow [BUILD_WITH_MTPZ.md](BUILD_WITH_MTPZ.md)
to configure authentication for a source build.
This did not perform a physical Zune authentication test.
