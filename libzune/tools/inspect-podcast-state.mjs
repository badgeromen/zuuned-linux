#!/usr/bin/env node
// Offline research only. No device access and no deletion decisions.
// Field locations are reference leads from XuneSyncLibrary, LGPL-2.1:
// https://github.com/xune-software/XuneSyncLibrary/tree/c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4/lib/src/zmdb
// ZuneClassicParser.cpp:545, ZuneHDParser.cpp:511, ZMDBTypes.h:218.
// This inspector is independently written; no reference implementation copied.
import { createHash } from 'node:crypto';
import { readFileSync, statSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

const MAX_BYTES = 256 * 1024 * 1024;
const REFERENCE = 'https://github.com/xune-software/XuneSyncLibrary/tree/c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4/lib/src/zmdb';
const sha256 = data => createHash('sha256').update(data).digest('hex');
const hex = n => `0x${n.toString(16).padStart(8, '0')}`;
function requireThat(condition, message) {
    if (!condition) throw new Error(message);
}

export function inspectPodcastState(container) {
    requireThat(Buffer.isBuffer(container), 'Expected a binary Buffer');
    requireThat(container.length >= 12 + 0x30 && container.length <= MAX_BYTES, 'Invalid container size');
    requireThat(container.readUInt32LE(0) === container.length, 'Incomplete container or trailing bytes');
    requireThat(container.readUInt16LE(4) === 2 && container.readUInt16LE(6) === 0x9217,
        'Expected a PTP data container for ZMDB opcode 0x9217');
    const data = container.subarray(12);
    requireThat(data.subarray(0, 4).equals(Buffer.from('ZMDB')), 'Missing ZMDB magic');
    requireThat(data.subarray(0x20, 0x24).equals(Buffer.from('ZMed')), 'Missing ZMed header');
    const version = data.readUInt16LE(0x24);
    requireThat(version === 2 || version === 5, 'Unsupported ZMed version');
    const candidates = [];
    for (let off = 0x30; off < 0x100 && off + 4 <= data.length; off += 4)
        if (data.subarray(off, off + 4).equals(Buffer.from('ZArr'))) candidates.push(off);
    // Each subsequent descriptor also starts with ZArr in real dumps.
    // The first aligned marker is descriptor zero, whose entry size is 8.
    requireThat(candidates.length > 0, 'Missing ZArr descriptor table');
    const descriptor = candidates[0];
    // Actual snapshots contain 33 descriptors on Classic and 97 on HD.
    // Walk complete ZArr headers rather than reserving a fixed HD-sized block.
    let descriptorEnd = descriptor;
    let descriptorCount = 0;
    while (descriptorEnd + 4 <= data.length &&
           data.subarray(descriptorEnd, descriptorEnd + 4).equals(Buffer.from('ZArr'))) {
        requireThat(descriptorEnd + 20 <= data.length, 'Truncated descriptor table');
        requireThat(++descriptorCount <= 256, 'Excessive descriptor table');
        descriptorEnd += 20;
    }
    requireThat(data.readUInt16LE(descriptor + 6) === 8, 'Invalid index entry size');
    const count = data.readUInt32LE(descriptor + 8);
    const indexOffset = data.readUInt32LE(descriptor + 16);
    const indexEnd = indexOffset + count * 8;
    requireThat(count <= 1000000 && indexOffset >= descriptorEnd && indexEnd <= data.length,
        'Invalid index bounds');
    const seenIds = new Set();
    const seenOffsets = new Set();
    const records = [];
    const titleOffset = version === 5 ? 0x24 : 0x20;
    const flagsOffset = version === 5 ? 0x20 : 0x1c;
    for (let i = 0; i < count; ++i) {
        const id = data.readUInt32LE(indexOffset + i * 8);
        const offset = data.readUInt32LE(indexOffset + i * 8 + 4);
        requireThat(!seenIds.has(id), 'Duplicate atom ID in index');
        seenIds.add(id);
        if ((id >>> 24) !== 0x10) continue;
        requireThat(!seenOffsets.has(offset), 'Duplicate podcast record offset');
        seenOffsets.add(offset);
        requireThat(offset >= descriptorEnd + 4 && offset <= data.length,
            'Invalid podcast record offset');
        const header = data.readUInt32LE(offset - 4);
        const size = header & 0xffffff;
        requireThat((header & 0x80000000) === 0, 'Invalid podcast record flag');
        requireThat(size > titleOffset && size <= data.length - offset, 'Invalid podcast record length');
        requireThat(offset + size <= indexOffset || offset - 4 >= indexEnd,
            'Podcast record overlaps index');
        const record = data.subarray(offset, offset + size);
        const end = record.indexOf(0, titleOffset);
        requireThat(end >= titleOffset, 'Missing podcast title terminator');
        let title;
        try { title = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(record.subarray(titleOffset, end)); }
        catch { throw new Error('Invalid UTF-8 podcast title'); }
        const flags = record.readUInt32LE(flagsOffset);
        records.push({
            atomId: hex(id), filenameRef: hex(record.readUInt32LE(0)),
            seriesId: hex(record.readUInt32LE(4)), title,
            durationMs: record.readUInt32LE(8), bookmarkMs: record.readUInt32LE(0x0c),
            rawFlags: hex(flags), candidateMarkedPlayed: (flags & 0x200) !== 0,
            recordOffset: offset, recordBytes: size, recordSha256: sha256(record)
        });
    }
    const byOffset = [...records].sort((a, b) => a.recordOffset - b.recordOffset);
    for (let i = 1; i < byOffset.length; ++i)
        requireThat(byOffset[i - 1].recordOffset + byOffset[i - 1].recordBytes <= byOffset[i].recordOffset - 4,
            'Overlapping podcast records');
    return {
        format: 'zuuned-podcast-state-research-v1',
        scope: 'Reference-only candidate fields. Not evidence of natural completion and never authorization to delete.',
        reference: REFERENCE, version, containerBytes: container.length,
        containerSha256: sha256(container), descriptorCount, indexEntries: count, records
    };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    try {
        requireThat(process.argv.length === 3, 'Usage: node tools/inspect-podcast-state.mjs <zunetool-zmdbdump.bin>');
        requireThat(statSync(process.argv[2]).size <= MAX_BYTES, 'Snapshot exceeds research size limit');
        const result = inspectPodcastState(readFileSync(process.argv[2]));
        process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
    } catch (error) {
        process.stderr.write(`Podcast snapshot rejected: ${error.message}\n`);
        process.exitCode = 2;
    }
}
