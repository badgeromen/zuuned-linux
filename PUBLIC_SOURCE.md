# Public source snapshot

This snapshot contains application and libzune source, without private Git history.
libzune is included as ordinary source, not a private Gitea submodule.

MTPZ certificate/key material is deliberately absent. This source builds without
embedded credentials. Zune authentication requires separately supplied local
credentials in ~/.mtpz-data. Local library and playback do not require them.
See [Building with Zune authentication](docs/BUILD_WITH_MTPZ.md) for obtaining
the external data file and creating a local embedded-data header. No credential
values are included in this source snapshot.

Bundled TMDB/Fanart API defaults are retained as requested. The existing
tmdbApiKey and fanarttvApiKey settings still allow overrides.

Existing AppImages built with embedded credentials still contain those credentials.
This snapshot is not represented as the exact source for those older binaries.
Retain all original and third-party license notices. This export is a technical
separation, not a conclusion about third-party redistribution rights.

Build on Linux using the dependencies in README.md:

    cmake -B build -G Ninja
    cmake --build build

