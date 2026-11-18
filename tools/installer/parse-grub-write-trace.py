#!/usr/bin/env python3
"""Reconcile GRUB block-device write syscalls with disposable image byte changes."""
import argparse
import glob
import json
import os
import re
import sys

FD_BLOCK = re.compile(r"\((\d+)<(/dev/[^<]+)<block\b")
LSEEK = re.compile(
    r"lseek\((\d+)<(/dev/[^<]+)<block [^>]+>>,\s*(-?\d+),\s*(SEEK_[A-Z]+)\)\s+=\s+(-?\d+)"
)
IOCTL = re.compile(r"ioctl\((\d+)<(/dev/[^<]+)<block [^>]+>>,\s*([A-Z0-9_]+)")
SAFE_BLOCK_IOCTLS = {
    "BLKFLSBUF", "BLKGETSIZE64", "BLKSSZGET", "BLKROGET",
    "BLKPBSZGET", "BLKIOMIN", "BLKIOOPT", "BLKALIGNOFF",
    "BLKGETDISKSEQ",
}


def merge_ranges(ranges):
    merged = []
    for start, length in sorted(ranges):
        if length <= 0:
            continue
        end = start + length
        if merged and start <= merged[-1][0] + merged[-1][1]:
            previous_start, previous_length = merged[-1]
            merged[-1] = (previous_start, max(previous_start + previous_length, end) - previous_start)
        else:
            merged.append((start, length))
    return merged


def changed_ranges(before_path, after_path):
    ranges = []
    offset = 0
    with open(before_path, "rb") as before, open(after_path, "rb") as after:
        while True:
            a, b = before.read(1024 * 1024), after.read(1024 * 1024)
            if not a and not b:
                break
            if len(a) != len(b):
                raise ValueError("before/after image sizes differ")
            i = 0
            while i < len(a):
                if a[i] == b[i]:
                    i += 1
                    continue
                start = i
                while i < len(a) and a[i] != b[i]:
                    i += 1
                ranges.append((offset + start, i - start))
            offset += len(a)
    return merge_ranges(ranges)


def parse_trace_files(trace_prefix, target, image_size):
    writes = []
    ioctls = []
    non_target_writes = []
    unclassified = []
    for trace_path in sorted(glob.glob(trace_prefix + ".*")):
        offsets = {}
        with open(trace_path, errors="replace") as trace:
            for line_no, line in enumerate(trace, 1):
                seek = LSEEK.search(line)
                if seek:
                    fd, device, _requested, _whence, result = seek.groups()
                    if device == target:
                        if int(result) < 0:
                            unclassified.append({"file": trace_path, "line": line_no, "reason": "failed lseek"})
                        else:
                            offsets[int(fd)] = int(result)
                    continue

                ioctl = IOCTL.search(line)
                if ioctl:
                    _fd, device, operation = ioctl.groups()
                    if device == target:
                        ioctls.append(operation)
                        if operation not in SAFE_BLOCK_IOCTLS:
                            unclassified.append({"file": trace_path, "line": line_no,
                                                 "reason": "unreviewed target ioctl " + operation})
                    continue

                fd_info = FD_BLOCK.search(line)
                if not fd_info:
                    continue
                fd, device = int(fd_info.group(1)), fd_info.group(2)
                if line.startswith("mmap(") and device == target and "PROT_WRITE" in line:
                    unclassified.append({"file": trace_path, "line": line_no,
                                         "reason": "writable mmap of target block device"})
                    continue
                if not re.match(r"^(write|pwrite64|pwritev|pwritev2|writev)\(", line):
                    if device == target and re.match(r"^(ftruncate|fallocate|splice|vmsplice|copy_file_range)\(", line):
                        unclassified.append({"file": trace_path, "line": line_no,
                                             "reason": "unreviewed target write-capable syscall"})
                    continue

                if device != target:
                    non_target_writes.append({"file": trace_path, "line": line_no, "device": device})
                    continue

                if line.startswith("pwrite64("):
                    tail = re.search(r",\s*(\d+),\s*(-?\d+)\)\s+=\s+(\d+)\s*$", line)
                    if not tail:
                        unclassified.append({"file": trace_path, "line": line_no, "reason": "unparsed pwrite64"})
                        continue
                    _count, position, result = map(int, tail.groups())
                elif line.startswith("pwritev("):
                    tail = re.search(r",\s*\d+,\s*(-?\d+)\)\s+=\s+(\d+)\s*$", line)
                    if not tail:
                        unclassified.append({"file": trace_path, "line": line_no, "reason": "unparsed pwritev"})
                        continue
                    position, result = map(int, tail.groups())
                elif line.startswith("pwritev2("):
                    unclassified.append({"file": trace_path, "line": line_no, "reason": "pwritev2 requires audit extension"})
                    continue
                else:
                    tail = re.search(r",\s*\d+\)\s+=\s+(\d+)\s*$", line)
                    if not tail:
                        unclassified.append({"file": trace_path, "line": line_no, "reason": "unparsed write syscall"})
                        continue
                    result = int(tail.group(1))
                    position = offsets.get(fd)
                    if position is None:
                        unclassified.append({"file": trace_path, "line": line_no,
                                             "reason": "write without known file offset"})
                        continue
                    offsets[fd] = position + result

                if result:
                    if position < 0 or position + result > image_size:
                        unclassified.append({"file": trace_path, "line": line_no,
                                             "reason": "write range outside target image"})
                    else:
                        writes.append((position, result))

    return {
        "write_operations": len(writes),
        "write_ranges": merge_ranges(writes),
        "target_ioctls": sorted(set(ioctls)),
        "non_target_block_device_writes": non_target_writes,
        "unclassified_target_writes": unclassified,
    }


def covered(changed, written):
    for start, length in changed:
        end = start + length
        if not any(wstart <= start and wstart + wlength >= end for wstart, wlength in written):
            return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--allow-empty", action="store_true")
    ap.add_argument("trace_prefix")
    ap.add_argument("target_device")
    ap.add_argument("image_sectors", type=int)
    ap.add_argument("before_image")
    ap.add_argument("after_image")
    ap.add_argument("output_json")
    args = ap.parse_args()

    image_size = args.image_sectors * 512
    observed = parse_trace_files(args.trace_prefix, args.target_device, image_size)
    changed = changed_ranges(args.before_image, args.after_image)
    ok = covered(changed, observed["write_ranges"])
    result = {
        "target_device": args.target_device,
        "logical_sector_size": 512,
        "image_sectors": args.image_sectors,
        "write_operations": observed["write_operations"],
        "write_ranges": [{"offset": x, "length": n} for x, n in observed["write_ranges"]],
        "changed_ranges": [{"offset": x, "length": n} for x, n in changed],
        "changed_bytes": sum(n for _, n in changed),
        "changed_bytes_covered_by_traced_writes": ok,
        "target_ioctls": observed["target_ioctls"],
        "non_target_block_device_writes": observed["non_target_block_device_writes"],
        "unclassified_target_writes": observed["unclassified_target_writes"],
    }
    with open(args.output_json, "w") as out:
        json.dump(result, out, indent=2)
        out.write("\n")
    print(json.dumps({k: result[k] for k in (
        "target_device", "write_operations", "write_ranges", "changed_bytes",
        "changed_bytes_covered_by_traced_writes", "target_ioctls",
        "non_target_block_device_writes", "unclassified_target_writes"
    )}, indent=2))
    empty_ok = args.allow_empty and not observed["write_operations"] and not changed
    if ((not observed["write_operations"] and not empty_ok) or not ok or
        observed["non_target_block_device_writes"] or observed["unclassified_target_writes"]):
        raise SystemExit("GRUB write trace did not reconcile safely")


if __name__ == "__main__":
    main()
