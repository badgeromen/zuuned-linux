# Spoken-media covers sent to the Zune

After building the application, run `node tests/spoken-cover-worker/run.mjs`.
The fixture links production native app objects with wrapped C device functions,
uses a sentinel handle and generated local covers, and never opens USB.

The 22 checks drive `DeviceWorker::doSendAudiobook` and `doSendPodcast` and
inspect the bytes handed to `zune_brand`. A 1600x1600 4:4:4 cover, like the
covers Zuuned stores, must arrive as a baseline 4:2:0 JPEG within the limits
`zune_get_brand_limits` reports: 240x240 for a Zune 30, 272x272 inside an
HD-style 480x272, at most 480 pixels when more is declared, and 240x240 when
nothing is declared or the limits cannot be read. Wide covers keep their shape,
small ones are not enlarged, EXIF orientation is applied, transparency becomes
black, and a byte limit lowers the quality until the cover fits. Covers that
cannot be decoded or fitted are reported and not sent; the uploaded part or
episode stays synced. Missing covers, refused covers and existing parts keep
their earlier behavior and warnings.

Against the previous send code (raw cover bytes) 12 of the 22 checks fail.
These checks do not show the cover on a device screen.
