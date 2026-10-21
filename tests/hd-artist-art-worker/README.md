# HD artist sync worker policy

After building the application, run `node tests/hd-artist-art-worker/run.mjs`.
The fixture links production native app objects with wrapped C device functions,
uses a sentinel handle and disposable local artwork, and never constructs the
USB-owning facade. It cannot access a physical device.

It covers ordinary and Classic sync, generation/serial/run guards, existing
cancellation, partial and complete per-asset results, biography progress,
identity-conflict fallback, attempted-create and transport uncertainty, failed
album/artist links, and no duplicate fallback after the callback runs.

These checks establish application decisions, not the HD firmware's request
trigger or artist cache refresh behavior.
