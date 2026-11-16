// A separate, deliberately non-destructive-first model for selecting existing DOS/MBR partitions.
// It is not the GPT manualmodel and is not yet an install-run authorization.
export const MBR_LIMITS = {
  root: 8 * 1024 ** 3,
  home: 1024 ** 3,
  swapMin: 256 * 1024 ** 2,
  swapMax: 64 * 1024 ** 3,
};

const num = (v) => Number.isInteger(v) && v > 0;
const bytes = (disk, p) => (p.end - p.start + 1) * disk.mbrLsec;

export const emptyMbrManual = () => ({ rootPart: 0, homePart: 0, swapPart: 0, homeFormat: false });

export function mbrPartitions(disk) {
  if (!disk || disk.mbrProbeOk !== true || !disk.mbrTag || disk.mbrLsec !== 512 ||
      !Array.isArray(disk.mbrParts)) return [];
  const fsByNum = new Map((disk.parts || []).filter((p) => num(p.num)).map((p) => [p.num, p.fs || p.fstype || '']));
  return disk.mbrParts
    .filter((p) => num(p.num) && Number.isSafeInteger(p.start) && Number.isSafeInteger(p.end) &&
      p.start >= 0 && p.end >= p.start && (p.type === 0x83 || p.type === 0x82))
    .map((p) => ({ ...p, fs: fsByNum.get(p.num) || '', bytes: bytes(disk, p) }))
    .sort((a, b) => a.start - b.start);
}

export function setMbrRole(selection, role, partNum) {
  if (!['rootPart', 'homePart', 'swapPart'].includes(role)) return selection;
  if (partNum !== 0 && !num(partNum)) return selection;
  const next = { ...selection, [role]: partNum };
  // One physical partition can have only one role.
  if (partNum) for (const key of ['rootPart', 'homePart', 'swapPart']) {
    if (key !== role && next[key] === partNum) next[key] = 0;
  }
  return next;
}

export function reviewMbrManual(disk, selection) {
  const parts = mbrPartitions(disk);
  const byNum = new Map(parts.map((p) => [p.num, p]));
  const errors = [];
  if (!disk || disk.mbrProbeOk !== true || !disk.mbrTag || disk.mbrLsec !== 512)
    errors.push('install.manual.mbr.unavailable');
  if (!selection || !num(selection.rootPart)) errors.push('install.manual.mbr.noRoot');
  const root = byNum.get(selection?.rootPart);
  if (selection?.rootPart && !root) errors.push('install.manual.mbr.noSuchPartition');
  else if (root && root.type !== 0x83) errors.push('install.manual.mbr.rootType');
  else if (root && root.bytes < MBR_LIMITS.root) errors.push('install.manual.mbr.rootTooSmall');

  const home = selection?.homePart ? byNum.get(selection.homePart) : null;
  if (selection?.homePart && !home) errors.push('install.manual.mbr.noSuchPartition');
  else if (home && home.type !== 0x83) errors.push('install.manual.mbr.homeType');
  else if (home && home.bytes < MBR_LIMITS.home) errors.push('install.manual.mbr.homeTooSmall');
  else if (home && !selection.homeFormat && !['ext4', 'ext3', 'ext2', 'xfs', 'btrfs'].includes(home.fs))
    errors.push('install.manual.mbr.homeUnknownFs');

  const swap = selection?.swapPart ? byNum.get(selection.swapPart) : null;
  if (selection?.swapPart && !swap) errors.push('install.manual.mbr.noSuchPartition');
  else if (swap && swap.type !== 0x82) errors.push('install.manual.mbr.swapType');
  else if (swap && (swap.bytes < MBR_LIMITS.swapMin || swap.bytes > MBR_LIMITS.swapMax))
    errors.push('install.manual.mbr.swapSize');

  const chosen = [selection?.rootPart, selection?.homePart, selection?.swapPart].filter(Boolean);
  if (new Set(chosen).size !== chosen.length) errors.push('install.manual.mbr.duplicateRole');
  return {
    errors,
    readyForBackend: errors.length === 0,
    // The UI must not send this plan to install-run until its independent backend path exists.
    fields: errors.length ? null : {
      mode: 'manual-mbr',
      mbrTag: disk.mbrTag,
      rootPart: selection.rootPart,
      homePart: selection.homePart || 0,
      swapPart: selection.swapPart || 0,
      rootFormat: true,
      homeFormat: selection.homePart ? !!selection.homeFormat : false,
    },
    selected: { root, home, swap },
  };
}
