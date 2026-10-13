#!/usr/bin/env node
import assert from 'node:assert/strict';
import { inspectPodcastState } from '../tools/inspect-podcast-state.mjs';

let passed = 0;
const descriptor = 0x38, index = 2000, offset = 2020;
function fixture(version = 5, flags = 0) {
    const descriptorCount = version === 2 ? 33 : 97;
    const index = descriptor + descriptorCount * 20 + 4;
    const offset = index + 20;
    const titleOffset = version === 5 ? 0x24 : 0x20;
    const title = Buffer.from('Episode π\0');
    const size = titleOffset + title.length;
    const b = Buffer.alloc(12 + offset + size);
    b.writeUInt32LE(b.length, 0); b.writeUInt16LE(2, 4); b.writeUInt16LE(0x9217, 6);
    const d = b.subarray(12);
    d.write('ZMDB'); d.write('ZMed', 0x20); d.writeUInt16LE(version, 0x24);
    for (let i = 0; i < descriptorCount; i++) d.write('ZArr', descriptor + i * 20);
    d.writeUInt16LE(8, descriptor + 6);
    d.writeUInt32LE(1, descriptor + 8); d.writeUInt32LE(index, descriptor + 16);
    d.writeUInt32LE(0x10000001, index); d.writeUInt32LE(offset, index + 4);
    d.writeUInt32LE(size, offset - 4); d.writeUInt32LE(0x05000001, offset);
    d.writeUInt32LE(0x0f000001, offset + 4);
    d.writeUInt32LE(100000, offset + 8); d.writeUInt32LE(12345, offset + 12);
    d.writeUInt32LE(flags, offset + (version === 5 ? 0x20 : 0x1c));
    title.copy(d, offset + titleOffset);
    return b;
}
function check(name, f) { f(); passed++; console.log(`PASS ${name}`); }
function reject(name, mutate, pattern) {
    check(name, () => { const b = fixture(); mutate(b, b.subarray(12)); assert.throws(() => inspectPodcastState(b), pattern); });
}
for (const version of [2, 5]) {
    for (const flags of [0, 1, 0x400, 0x200, 0x201]) {
        check(`version ${version} flags ${flags}`, () => {
            const result = inspectPodcastState(fixture(version, flags));
            assert.equal(result.version, version);
            assert.equal(result.descriptorCount, version === 2 ? 33 : 97);
            assert.equal(result.records.length, 1);
            const row = result.records[0];
            assert.equal(row.candidateMarkedPlayed, (flags & 0x200) !== 0);
            assert.equal(row.title, 'Episode π'); assert.equal(row.bookmarkMs, 12345);
            assert.equal(row.durationMs, 100000); assert.equal(row.seriesId, '0x0f000001');
            assert.equal(row.recordSha256.length, 64); assert.equal(result.containerSha256.length, 64);
            assert.match(result.scope, /never authorization to delete/);
        });
    }
}
check('truncated container', () => assert.throws(() => inspectPodcastState(fixture().subarray(0, -1)), /Incomplete/));
check('trailing container bytes', () => assert.throws(() => inspectPodcastState(Buffer.concat([fixture(), Buffer.alloc(1)])), /trailing/));
check('tiny header', () => assert.throws(() => inspectPodcastState(Buffer.alloc(10)), /size/));
check('truncated descriptor table', () => {
    const b = fixture().subarray(0, 300); b.writeUInt32LE(b.length, 0);
    assert.throws(() => inspectPodcastState(b), /descriptor table/);
});
check('empty podcast inventory is not played evidence', () => {
    const b = fixture(); b.writeUInt32LE(0, 12 + descriptor + 8);
    assert.deepEqual(inspectPodcastState(b).records, []);
});
reject('wrong container type', b => b.writeUInt16LE(3, 4), /PTP/);
reject('wrong opcode', b => b.writeUInt16LE(0x1009, 6), /PTP/);
reject('wrong ZMDB magic', (b, d) => d.write('FAIL'), /magic/);
reject('wrong ZMed magic', (b, d) => d.write('FAIL', 0x20), /ZMed/);
reject('future version', (b, d) => d.writeUInt16LE(6, 0x24), /version/);
reject('missing descriptor table', (b, d) => {
    for (let off = descriptor; off < 0x100; off += 20) d.write('FAIL', off);
}, /ZArr/);
reject('damaged first descriptor cannot use a later descriptor as the index',
    (b, d) => d.write('FAIL', descriptor), /index entry size/);
check('repeated ZArr descriptor headers', () => {
    const b = fixture(); b.write('ZArr', 12 + descriptor + 20);
    assert.equal(inspectPodcastState(b).records.length, 1);
});
reject('wrong index entry size', (b, d) => d.writeUInt16LE(4, descriptor + 6), /entry size/);
reject('index overflow', (b, d) => d.writeUInt32LE(0xffffffff, descriptor + 8), /bounds/);
reject('index in header', (b, d) => d.writeUInt32LE(4, descriptor + 16), /bounds/);
reject('duplicate atom IDs', (b, d) => { d.writeUInt32LE(2, descriptor + 8); d.copy(d, index + 8, index, index + 8); }, /Duplicate atom/);
reject('aliased podcast records', (b, d) => {
    d.writeUInt32LE(2, descriptor + 8); d.writeUInt32LE(0x10000002, index + 8);
    d.writeUInt32LE(offset, index + 12);
}, /Duplicate podcast record/);
reject('record offset past EOF', (b, d) => d.writeUInt32LE(d.length + 1, index + 4), /offset/);
reject('record offset in header', (b, d) => d.writeUInt32LE(2, index + 4), /offset/);
reject('record length past EOF', (b, d) => d.writeUInt32LE(0xffffff, offset - 4), /length/);
reject('record too short', (b, d) => d.writeUInt32LE(20, offset - 4), /length/);
reject('invalid record flag', (b, d) => d.writeUInt32LE((d.readUInt32LE(offset - 4) | 0x80000000) >>> 0, offset - 4), /flag/);
reject('missing title NUL', (b, d) => d[d.length - 1] = 65, /terminator/);
reject('malformed UTF-8 title', (b, d) => d[offset + 0x24] = 0xff, /UTF-8/);
console.log(`${passed} podcast snapshot checks passed`);
