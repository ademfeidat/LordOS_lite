import assert from 'node:assert/strict';
import { emptyMbrManual, mbrPartitions, setMbrRole, reviewMbrManual, MBR_LIMITS } from '../apps/installer/mbrmanualmodel.js';

const GiB = 1024 ** 3;
const disk = {
  name: 'sda', lsec: 512, mbrProbeOk: true, mbrTag: 'mbr-tag-abc',
  mbrParts: [
    { name: 'sda1', num: 1, start: 2048, end: 2048 + 20 * GiB / 512 - 1, type: 0x83, logical: false },
    { name: 'sda5', num: 5, start: 2048 + 20 * GiB / 512, end: 2048 + 36 * GiB / 512 - 1, type: 0x83, logical: true },
    { name: 'sda6', num: 6, start: 2048 + 36 * GiB / 512, end: 2048 + 37 * GiB / 512 - 1, type: 0x82, logical: true },
    { name: 'sda2', num: 2, start: 2048 + 37 * GiB / 512, end: 2048 + 39 * GiB / 512 - 1, type: 0x07, logical: false },
  ],
  parts: [{ num: 1, fs: 'ext4' }, { num: 5, fs: 'ext4' }, { num: 6, fs: 'swap' }],
};
assert.equal(mbrPartitions(disk).length, 3); // unsupported NTFS type is not offered
let s = emptyMbrManual();
assert.deepEqual(reviewMbrManual(disk, s).errors, ['install.manual.mbr.noRoot']);
s = setMbrRole(s, 'rootPart', 1);
s = setMbrRole(s, 'homePart', 5);
s = setMbrRole(s, 'swapPart', 6);
let r = reviewMbrManual(disk, s);
assert.deepEqual(r.errors, []);
assert.deepEqual(r.fields, {
  mode: 'manual-mbr', mbrTag: 'mbr-tag-abc', rootPart: 1, homePart: 5,
  swapPart: 6, rootFormat: true, homeFormat: false,
});
assert.equal(setMbrRole(s, 'homePart', 1).rootPart, 0); // unique role assignment
assert.equal(setMbrRole(s, 'homePart', 1).homePart, 1);
assert.ok(reviewMbrManual(disk, setMbrRole(s, 'rootPart', 2)).errors.includes('install.manual.mbr.noSuchPartition'));
assert.ok(reviewMbrManual(disk, setMbrRole(s, 'rootPart', 6)).errors.includes('install.manual.mbr.rootType'));
assert.ok(reviewMbrManual(disk, setMbrRole(s, 'swapPart', 5)).errors.includes('install.manual.mbr.swapType'));
assert.ok(reviewMbrManual(disk, { ...s, homeFormat: true }).fields.homeFormat);
assert.ok(reviewMbrManual({ ...disk, mbrProbeOk: false }, s).errors.includes('install.manual.mbr.unavailable'));
assert.ok(reviewMbrManual({ ...disk, lsec: 4096 }, s).errors.includes('install.manual.mbr.unavailable'));
assert.ok(reviewMbrManual({ ...disk, mbrTag: '' }, s).errors.includes('install.manual.mbr.unavailable'));
const tooSmall = { ...disk, mbrParts: disk.mbrParts.map((p) => p.num === 1 ? { ...p, end: p.start + MBR_LIMITS.root / 512 - 2 } : p) };
assert.ok(reviewMbrManual(tooSmall, s).errors.includes('install.manual.mbr.rootTooSmall'));
const unknownHome = { ...disk, parts: disk.parts.map((p) => p.num === 5 ? { ...p, fs: 'unknown' } : p) };
assert.ok(reviewMbrManual(unknownHome, s).errors.includes('install.manual.mbr.homeUnknownFs'));
console.log('installer-mbr-manual-model: ok');
