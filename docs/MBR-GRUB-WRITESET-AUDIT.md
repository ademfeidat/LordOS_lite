# BIOS/MBR GRUB write-set audit gate

Status: **not implemented; no disk-write authorization**.

This document defines the evidence required before LordOS may add a BIOS GRUB installation path for an existing DOS/MBR disk. A calculated embedding gap is not a write-set, and a sector backup limited to LBA 0 plus that gap must not be treated as complete until the shipped GRUB binary has been observed and its writes independently verified.

## Current source facts

- The amd64 ISO build requests grub-common, grub2-common, and grub-pc-bin through PKGS_INSTALLER in tools/iso/build.sh.
- Those package names are not version-pinned in the build script. The exact GRUB implementation therefore depends on the Debian repository state used for a particular ISO build; this branch cannot honestly claim one universal BIOS GRUB write-set yet.
- The current BIOS command in server/src/install/install.c is grub-install --target=i386-pc <disk>, and it is reached only by the non-shared erase-install branch. The shared/manual path is EFI-only.
- The MBR guard is planning-only and returns writes_authorized = 0. manual-mbr remains rejected by the install-plan validator.

## Required provenance

Every audit run must record, as machine-readable artifacts:

1. The exact Debian release, architecture, repository snapshot timestamp, and versions of grub-common, grub2-common, and grub-pc-bin.
2. SHA-256 hashes of the relevant executables and modules, including grub-install, grub-bios-setup, and the selected i386-pc modules.
3. The exact command line, environment, target disk geometry, logical sector size, partition-table bytes, and GRUB build configuration.
4. The complete set of byte ranges written, not merely the sectors expected from a traditional MBR installation.

A package update, GRUB executable hash change, module change, option change, or target geometry change invalidates the previous audit until rerun.

## Disposable-image experiment

The experiment must operate only on a newly created regular-file disk image owned by the test job. It must never discover or select a host block device. If a loop device is used, the test must create it from that exact image, verify its backing-file identity immediately before use, and detach it in a reliable cleanup path.

Capture all write-capable operations performed by the complete grub-install process tree, including helper processes and writes through file descriptors, mappings, or block-device ioctls. Trace output is evidence to be cross-checked against before/after byte comparisons; tracing alone is not sufficient. Compare the entire image before and after installation and derive the changed byte ranges. The observed ranges must agree with the system-call trace. Any unexplained changed byte, unobserved write, or non-deterministic range fails the audit.

Run the experiment against at least:

- a DOS/MBR image with a conventional first partition at 1 MiB;
- a DOS/MBR image with a smaller but otherwise valid post-MBR gap;
- a DOS/MBR image with a logical partition and EBR chain;
- an image containing non-zero sentinel data throughout the embedding gap;
- a disk image with deliberately insufficient embedding space.

The sentinel case must demonstrate whether GRUB overwrites pre-existing gap content. Such content must be treated as user data unless the installer has an explicit, separately justified policy to replace it.

## Automated audit harness status

A CI-only prototype now exists at
tools/installer/audit-grub-mbr-writeset.sh, invoked by
.github/workflows/grub-mbr-writeset-audit.yml. It creates regular-file images
inside the ephemeral GitHub runner, attaches only those files as loop devices,
checks each loop device's backing-file identity, fills the embedding gap with
sentinel bytes, traces the GRUB process tree, and compares complete before/after
images. It records package versions, executable hashes, traces, and changed
byte ranges as workflow artifacts.

The current prototype covers a conventional 1 MiB gap and one reduced-gap
case. It does not yet cover logical/EBR layouts, a guaranteed too-small gap,
full syscall-to-byte-range reconciliation, or every relevant GRUB module hash.
A successful run is evidence only for the exact package versions and
environment recorded in its artifact; it is not a release approval and does
not authorize writes to real disks.

### First disposable-image observation (not release approval)

Run 44360623470 completed successfully on commit
3bb41ef4a503b5047fa3ea8de775d38f29ebf093. It used Ubuntu 26.04.1 LTS and
GRUB 2.14-2ubuntu2.1; grub-install and grub-bios-setup hashes and the full
before/after images are retained in artifact
14454485806 (grub-mbr-write-set-evidence).

Both the 1 MiB-gap image (first partition LBA 2048) and the reduced-gap image
(first partition LBA 256) completed grub-install successfully. Byte comparison
found changes in sectors LBA 0 through LBA 118 (119 sectors total) in both
cases. The MBR boot-code area changed, while the disk signature, reserved MBR
bytes, partition entries, and MBR signature remained byte-identical. The first
partition's contents remained unchanged. The sentinel-filled gap was
overwritten by GRUB data.

This is a warning, not permission: a successful GRUB install overwrote
pre-existing gap bytes in both test layouts. The 119-sector observation is
specific to this GRUB package build, module set, command, and geometry. The
first trace captured 119 512-byte write calls plus device ioctls, but did not
yet include seek syscalls; the harness is being extended to reconcile every
write call with its exact byte offset. No backup or rollback implementation
has been accepted, and no real disk was used.

## Backup and rollback acceptance

Only after the write-set is stable may backup/restore code be designed. It must:

- back up every byte range that the exact GRUB run can modify, including sector 0 and every embedding or auxiliary range observed;
- store range offsets, lengths, disk geometry, package provenance, and a cryptographic digest for each backup object;
- use exclusive, no-follow file creation in a private directory and durable writes (fsync of file and containing directory);
- read the backup back and verify it before the first disk write;
- compare the target's live identity and complete pre-write bytes against the audited snapshot immediately before writing;
- restore every affected range after any failure, then read back and verify byte-for-byte equality with the original image;
- report rollback failure distinctly and never claim recovery unless the read-back comparison succeeds.

Fault injection must stop execution after each individual write boundary and at each persistence boundary (backup creation, backup sync, first write, subsequent writes, and final verification). Every injected failure must demonstrate exact restoration. Include process termination and simulated power-loss cases where the test harness can model them.

A successful rollback test on a regular-file image is necessary but not sufficient for real-device support. Real-device authorization additionally requires independent ancestry/busy checks, stable device identity, a fresh MBR/EBR fingerprint, and an explicit user acknowledgement that an existing boot manager may be replaced.

## Release gate

Do not connect this work to install-run, remove the explicit install.manual.mbr.notReady rejection, or expose the MBR install option until all of the following exist:

- version-specific GRUB write-set audit artifacts;
- an independently verified byte-range backup/restore implementation;
- disposable-image tests for install, byte preservation, and rollback;
- fail-closed handling for unknown GRUB versions and unknown write ranges;
- VM coverage for BIOS boot after successful install and after rollback.

No experiment described here has been run yet. This document is a test contract, not a claim that BIOS GRUB backup, installation, or rollback works.
