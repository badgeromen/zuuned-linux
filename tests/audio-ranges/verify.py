"""Generated audio, never user media. FFmpeg only makes/decodes test fixtures."""
import array
import math
import os
import pathlib
import subprocess
import sys
import wave

binary, temporary = sys.argv[1:]
root = pathlib.Path(temporary)
checks = 0

def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

def ffmpeg(*args):
    return run('ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error', *args)

def check(condition, message):
    global checks
    if not condition:
        raise AssertionError(message)
    checks += 1
    print('PASS', message)

# Non-packet-aligned boundaries and three distinct audible frequencies.
source = root / 'source.wav'
rate = 48000
boundaries = [0, 1337, 2791, 4100]
with wave.open(str(source), 'wb') as out:
    out.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
    samples = array.array('h')
    for i in range(rate * 4100 // 1000):
        time = i / rate
        frequency = 311 if time < 1.337 else (733 if time < 2.791 else 1297)
        samples.append(round(18000 * math.sin(2 * math.pi * frequency * time)))
    out.writeframes(samples.tobytes())
encoded = root / 'source.m4b'
ffmpeg('-i', str(source), '-c:a', 'aac', '-b:a', '192k', '-f', 'ipod', str(encoded))

def decode(path, output_rate=44100):
    result = array.array('f')
    result.frombytes(ffmpeg('-i', str(path), '-map', '0:a:0', '-ac', '1', '-ar', str(output_rate),
                           '-f', 'f32le', 'pipe:1'))
    return result

def energy(signal, hz, sample_rate=44100):
    real = sum(value * math.cos(2 * math.pi * hz * i / sample_rate) for i, value in enumerate(signal))
    imag = sum(value * math.sin(2 * math.pi * hz * i / sample_rate) for i, value in enumerate(signal))
    return real * real + imag * imag

for media in (source, encoded):
    lengths = []
    for chapter, (start, end) in enumerate(zip(boundaries, boundaries[1:])):
        destination = root / f'{media.suffix}-{chapter}.mp3'
        run(binary, str(media), str(start), str(end), str(destination), 'ok')
        data = decode(destination)
        lengths.append(len(data))
        expected = (end - start) * 44100 / 1000
        check(abs(len(data) - expected) <= 1.1, f'{media.suffix} chapter {chapter}: sample-accurate decoded duration')
        # Exclude only MP3 edge ringing, not a whole codec packet.
        middle = data[441:-441]
        frequencies = [311, 733, 1297]
        wanted = energy(middle, frequencies[chapter])
        other = max(energy(middle, hz) for hz in frequencies if hz != frequencies[chapter])
        check(wanted > other * 1000, f'{media.suffix} chapter {chapter}: correct audible content')
        # Direct phase correlation with the full decoded source catches a
        # packet-rounded start even where a single tone hides a missing slice.
        reference = decode(media)
        offset = round(start * 44.1)
        ref = reference[offset:offset + len(data)]
        n = min(len(middle), len(ref) - 882)
        numerator = sum(middle[i] * ref[i + 441] for i in range(n))
        denom = math.sqrt(sum(v*v for v in middle[:n]) * sum(v*v for v in ref[441:441+n]))
        check(numerator / denom > .975, f'{media.suffix} chapter {chapter}: nonzero seek preserves sample phase')
    check(abs(sum(lengths) - 4100 * 44.1) <= 3, f'{media.suffix}: adjacent chapter durations cover the complete book')

for start, end in [(-1, 100), (0, 0), (200, 100), (4000, 5000), (5000, 6000),
                   (0, 9223372036854775807)]:
    run(binary, str(encoded), str(start), str(end), str(root / 'invalid.mp3'), 'invalid')
    check(not (root / 'invalid.mp3').exists(), f'invalid range {start}:{end} refused without output')
for mode in ('cancel', 'pre-cancel'):
    before = set(pathlib.Path('/tmp').glob('zuuned_chapter_*.mp3'))
    run(binary, str(encoded), '0', '4100', str(root / 'cancel.mp3'), mode)
    check(not (root / 'cancel.mp3').exists() and before == set(pathlib.Path('/tmp').glob('zuuned_chapter_*.mp3')),
          mode + ' removes partial output')
# A rounded millisecond endpoint can extend a fraction of a millisecond past
# the last sample. Keep all real samples without inventing silence.
fractional = root / 'fractional.wav'
with wave.open(str(fractional), 'wb') as out:
    out.setparams((1, 2, 48000, 0, 'NONE', 'not compressed'))
    out.writeframes(array.array('h', [4000] * 196830).tobytes())
for media in (fractional, root / 'fractional.m4b'):
    if media.suffix == '.m4b':
        ffmpeg('-i', str(fractional), '-c:a', 'aac', '-f', 'ipod', str(media))
    destination = root / (media.name + '-end.mp3')
    run(binary, str(media), '3791', '4101', str(destination), 'ok')
    decoded = decode(destination)
    check(abs(len(decoded) - (4100.625 - 3791) * 44.1) <= 20,
          media.suffix + ': final rounded chapter endpoint keeps real audio')

playlist = root / 'external.ffconcat'
playlist.write_text("ffconcat version 1.0\nfile 'source.wav'\n")
fifo = root / 'pipe'
os.mkfifo(fifo)
for name, media in [('directory', root), ('FIFO', fifo), ('external playlist', playlist),
                    ('URL', 'https://example.invalid/book.m4b')]:
    run(binary, str(media), '0', '100', str(root / 'unsafe.mp3'), 'invalid')
    check(not (root / 'unsafe.mp3').exists(), name + ' refused as self-contained local audio')
print(f'{checks} audio range checks passed')
