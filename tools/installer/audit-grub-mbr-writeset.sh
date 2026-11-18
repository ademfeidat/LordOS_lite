#!/usr/bin/env bash
set -Eeuo pipefail
umask 077

if [[ ${EUID} -ne 0 ]]; then
  echo "Run this audit as root inside an isolated CI runner." >&2
  exit 2
fi
for tool in losetup sfdisk strace grub-install python3 sha256sum dpkg-query; do
  command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 2; }
done

OUT="${1:-artifacts/grub-mbr-audit}"
mkdir -p "$OUT"
OUT="$(realpath "$OUT")"
WORK="$(mktemp -d)"
LOOP=""
cleanup() {
  if [[ -n "$LOOP" ]]; then
    losetup -d "$LOOP" || true
  fi
  rm -rf "$WORK"
}
trap cleanup EXIT

run_case() {
  local name="$1" first_lba="$2" sectors="$3"
  local image="$WORK/$name.img"
  local boot="$WORK/$name-boot"
  local loop trace rc backing
  truncate -s $((sectors * 512)) "$image"
  loop="$(losetup --find --show --partscan "$image")"
  LOOP="$loop"
  backing="$(losetup --noheadings --raw --output BACK-FILE "$loop" | xargs)"
  [[ "$(realpath "$backing")" == "$(realpath "$image")" ]] || {
    echo "Loop backing-file identity mismatch" >&2; return 20;
  }

  sfdisk --wipe always --wipe-partitions always "$loop" <<EOF
label: dos
unit: sectors

start=$first_lba, size=$((sectors - first_lba - 1)), type=83
EOF
  partprobe "$loop" || true

  # Put recognizable data in the post-MBR gap; this is a disposable image.
  python3 - "$image" "$first_lba" <<'PY'
import sys
path, first = sys.argv[1], int(sys.argv[2])
with open(path, "r+b", buffering=0) as f:
    f.seek(512)
    remaining = (first - 1) * 512
    block = bytes([0xA5]) * 65536
    while remaining:
        chunk = block[:min(len(block), remaining)]
        f.write(chunk)
        remaining -= len(chunk)
    f.flush()
PY
  sync
  cp --reflink=never "$image" "$OUT/$name.before.img"
  trace="$OUT/$name.strace"
  mkdir -p "$boot"
  set +e
  strace -ff -yy -s 256 -o "$trace" \
    -e trace=write,pwrite64,pwritev,writev,truncate,ftruncate,ioctl,mmap,msync,openat,open,close \
    grub-install --target=i386-pc --boot-directory="$boot" --recheck "$loop"
  rc=$?
  set -e
  sync
  cp --reflink=never "$image" "$OUT/$name.after.img"

  python3 - "$OUT/$name.before.img" "$OUT/$name.after.img" "$OUT/$name.ranges.json" <<'PY'
import json, sys
before, after, output = sys.argv[1:]
ranges = []
offset = 0
with open(before, "rb") as a, open(after, "rb") as b:
    while True:
        x, y = a.read(1024 * 1024), b.read(1024 * 1024)
        if not x and not y:
            break
        if len(x) != len(y):
            raise SystemExit("image size changed")
        i = 0
        while i < len(x):
            if x[i] == y[i]:
                i += 1
                continue
            start = i
            while i < len(x) and x[i] != y[i]:
                i += 1
            absolute = offset + start
            length = i - start
            if ranges and ranges[-1]["offset"] + ranges[-1]["length"] == absolute:
                ranges[-1]["length"] += length
            else:
                ranges.append({"offset": absolute, "length": length})
        offset += len(x)
with open(output, "w") as f:
    json.dump(ranges, f, indent=2)
    f.write("\n")
PY

  python3 - "$OUT/$name.ranges.json" "$name" "$rc" <<'PY'
import json, sys
ranges = json.load(open(sys.argv[1]))
name, rc = sys.argv[2], int(sys.argv[3])
if name == "conventional-1mib" and (rc != 0 or not ranges):
    raise SystemExit("Conventional GRUB install did not succeed with observable disk changes")
PY

  grep -hF "$loop" "$trace"* > "$OUT/$name.loop-writes.txt" || true
  if [[ ! -s "$OUT/$name.loop-writes.txt" && "$rc" -eq 0 ]]; then
    echo "Successful GRUB run had no traced operations against $loop" >&2
    return 21
  fi

  {
    echo "case=$name"
    echo "first_partition_lba=$first_lba"
    echo "image_sectors=$sectors"
    echo "logical_sector_size=512"
    echo "loop_device=$loop"
    echo "grub_install_exit=$rc"
    echo "debian_version=$( . /etc/os-release; echo "${PRETTY_NAME:-unknown}" )"
    for pkg in grub-common grub2-common grub-pc-bin; do
      echo "$pkg=$(dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null || echo missing)"
    done
    sha256sum "$(command -v grub-install)" "$(command -v grub-bios-setup)" 2>/dev/null || true
  } > "$OUT/$name.metadata.txt"

  losetup -d "$loop"
  LOOP=""
  # The reduced-gap case is diagnostic: GRUB may reject it or may fit. Record
  # either outcome; only the conventional baseline is a hard success gate.
  return 0
}

# The first case uses a conventional 1 MiB gap. The second deliberately
# reduces the gap and records whether this exact package build still embeds.
run_case conventional-1mib 2048 262144
run_case reduced-gap 256 262144

# The audit runs as root to manage its private loop devices; make evidence
# readable by the unprivileged upload-artifact action after cleanup.
chmod -R a+rX "$OUT"
