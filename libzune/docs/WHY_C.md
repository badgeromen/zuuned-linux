# Why C?

libzune keeps device communication in a C99 library so applications can choose
their own interface and language. C++ includes the public header directly;
other languages can use their C foreign-function interface. The repository does
not currently ship or certify separate language bindings.

## What this choice provides

- A small interface of opaque handles, ordinary structs, callbacks and explicit
  allocation ownership, without requiring a UI framework.
- Direct access to libusb on Linux and the existing DriverKit backend on macOS.
- Shared PTP/MTP/MTPZ behavior for multiple applications, with protocol fixes in
  one library rather than separate implementations per UI.
- Native tools such as `zunetool` for testing the protocol without the application.

C is not a substitute for careful lifetime management. Callers must serialize
operations, retain borrowed scan data while searching, release allocations and
wait for workers before teardown. Applications supply asynchronous dispatch;
the library's calls block.

## Dependencies and limits

Linux builds link libusb, libgcrypt and, by default, libavformat/libavutil for
metadata probing. The macOS backend requires a separate DriverKit extension.
Legacy preprocessing and thumbnail helpers invoke external tools; the audio/video
transcode helpers currently assume Homebrew's FFmpeg location. See the
[README](../README.md) for exact build requirements and the
[API reference](API_REFERENCE.md) for ownership and portability limits.

These are design choices for this project, not benchmark claims about other
implementations. Performance and hardware support should be reported from
specific measurements and real-device tests.
