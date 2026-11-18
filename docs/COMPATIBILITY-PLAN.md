# LordOS Lite compatibility work

This document tracks the compatibility work on the LordOS fork. It is deliberately separate
from the upstream release claims: a target is not considered supported until its build and
installation path has been tested.

## Preserve the platform

LordOS Lite keeps the upstream LordOS compositor, display server, QuickJS/React UI engine,
native applications, themes, assets and security model. Compatibility work must not replace
the desktop with XFCE, GNOME, LXQt, Openbox or another desktop environment.

## Target matrix

| Area | Target | Status at start of this work |
|---|---|---|
| PC firmware | x86-64 UEFI | Upstream supported |
| PC firmware | x86-64 legacy BIOS | Live ISO and erase-install path supported upstream |
| Partition table | GPT | Erase install supported; free-space and manual paths currently require GPT |
| Partition table | MBR/DOS | Not supported by free-space/manual installer paths |
| Hybrid MBR | Any | Refuse safely; never attempt an automatic repair |
| ARM64 | UEFI | Separate experimental path; not a substitute for PC BIOS support |
| CPU | x86-64 Intel/AMD | Supported architecture; individual old CPUs still need testing |
| CPU | 32-bit x86 | Out of scope for the current upstream architecture |
| Memory | 3 GiB | Optimization target; upstream currently documents 4 GiB minimum |
| Secure Boot | UEFI Secure Boot | Upstream ISO currently requires it disabled |

## Work packages

### 1. Installer capability model

- Detect firmware mode, architecture, logical sector size and partition-table type independently.
- Report a specific reason when an install mode is unavailable.
- Keep erase, install-in-free-space and manual partitioning as distinct plans.
- Never silently convert an existing MBR disk to GPT in a non-erase install.
- Do not write to a disk until the exact proposed operations have been reviewed and confirmed.

### 2. Bootloader and partitioning

- Keep the existing GPT + UEFI and GPT + BIOS erase paths working.
- Design explicit MBR + BIOS support for manual installation, including primary/logical partition limits,
  extended partitions, partition numbering and GRUB embedding constraints.
- Evaluate UEFI on MBR only where the firmware exposes a valid FAT EFI System Partition; do not assume
  every UEFI implementation accepts every MBR layout.
- Make shared-install bootloader placement reversible and never overwrite another OS's EFI files.
- Refuse hybrid, damaged, ambiguous or concurrently modified tables.

### 3. Hardware compatibility

- Inventory the kernel, firmware and Mesa coverage already shipped in the ISO.
- Add a safe graphics fallback and document devices for which KMS is unavailable.
- Test older Intel and AMD CPUs against the actual minimum instruction set used by the kernel and
  LordOS binaries; do not claim support for 32-bit x86.
- Test SATA HDD, SATA SSD, NVMe and USB boot paths.

### 4. Low-memory profile

- Measure idle and workload memory before changing defaults.
- Evaluate zram, conservative services and browser/app memory behavior for 3 GiB systems.
- Keep the full LordOS desktop and security boundaries; do not remove core components merely to
  make a benchmark look smaller.

### 5. Verification gates

- Unit tests for every partition-table and firmware decision.
- Installer plan tests that prove malformed or ambiguous input is rejected.
- VM matrix: BIOS/GPT, UEFI/GPT, BIOS/MBR, and UEFI/MBR where the virtual firmware supports it.
- Test both non-destructive shared installation and deliberate erase installation on disposable disks.
- Build the ISO with the upstream `tools/iso/build.sh`; publish no image until checksums and boot tests pass.

## Safety rule

Partition-table conversion, formatting and bootloader installation can destroy data or make another
OS unbootable. A compatibility mode must fail closed when it cannot prove the exact disk state and
the exact changes it is about to make. Backups and disposable test disks are required for destructive
test cases.


## Implementation audit (2026-11-16)

The first source audit confirms that the installer has a GPT-only shared-install architecture,
not merely a UI restriction:

- `manual.c` rejects non-UEFI boots and every table except a clean protective-MBR GPT.
- `freepart.c` snapshots and validates both GPT headers and both GPT entry arrays, then uses
  `sgdisk` for partition-table writes. Its snapshot cannot represent a DOS/MBR table.
- `manualpart.c` uses the same GPT snapshot/backup/proof path as free-space installation.
- `install.c` has an x86-64 BIOS GRUB path for **erase installation** only. The shared-install
  paths install an EFI loader and register it with `efibootmgr`.
- The current manual-partition test suite is a pure decision test and is a suitable place to add
  firmware/table policy tests before any disk-writing code is introduced.
- The existing disk-table code deliberately classifies hybrid MBR separately; that refusal must
  remain in place.

### Consequence for the implementation

Do not enable MBR by changing only `mp_disk_why()`: the downstream snapshot, backup, table writer,
post-write proof, partition-number handling and bootloader path are all GPT/UEFI-specific.
A safe MBR implementation needs its own read-only parser and snapshot format, a DOS-aware writer
with an explicit primary/extended/logical model, and a matching byte-level post-write verifier.
It must not call `sgdisk` on a DOS partition table.

### Revised implementation order

1. Add pure MBR parsing and geometry tests (including invalid signatures, empty entries,
   overlaps, out-of-range sectors, primary entries and extended-container boundaries).
2. Add a disk snapshot abstraction that can represent GPT and DOS without treating a DOS disk
   as a damaged GPT disk.
3. Add a DOS-aware plan validator and writer only after the parser and tests cover primary,
   extended and logical partition numbering. Refuse plans that cannot be proven safe.
4. Add BIOS shared-install bootloader handling only after verifying GRUB embedding space and
   preserving existing boot code; otherwise keep BIOS shared-install unavailable.
5. Add UEFI-on-MBR as a separately gated mode only when a readable FAT ESP and firmware support
   are positively established.
6. Run the existing manual/free-space tests plus new parser tests and disposable-VM install tests.

No installer behavior is changed by this audit. MBR shared installation remains unavailable until
all of the above gates pass.

## MBR parser milestone (2026-11-16)

A first read-only DOS/MBR parser now exists in `server/src/install/mbr.c` and
`server/src/install/mbr.h`. It validates the 0x55AA signature, primary entry fields,
boot flags, disk bounds, overlapping primary extents, and the single extended-container
entry. It classifies a protective GPT MBR separately from a hybrid MBR. The parser performs
no device I/O and cannot change a disk.

A focused test suite is in `server/tests/mbr_test.c`, wired into the server's `make test`
target, with an isolated GitHub Actions job in `.github/workflows/installer-mbr.yml`.
A local strict compile with `-Wall -Wextra -Werror` and Address/UndefinedBehavior sanitizers
passed an equivalent 15-case smoke harness (0 failures). The committed full test suite is
also ran successfully in GitHub Actions (workflow run 43958643752; compile and test steps both succeeded).

**Important limits:** this milestone does not enumerate logical partitions inside an extended
partition, does not build an install plan, does not write an MBR, and is not connected to the
installer UI or install path. It does not enable MBR installation. Those steps remain blocked
until EBR-chain validation, DOS partition numbering, a safe reversible writer, and BIOS GRUB
embedding checks are implemented and tested.

### EBR-chain parser milestone

The read-only parser now also follows DOS extended-partition EBR chains through an injected
sector-reader callback. It assigns logical partition numbers from 5, validates EBR signatures,
relative offsets, 32-bit LBA limits, extended-container bounds, logical extents, overlap with
other logical partitions, and repeated EBR locations. The callback boundary keeps parsing
testable without opening or modifying a real disk.

The focused test suite now covers a two-entry logical chain, a cyclic link, an invalid EBR
signature, a missing reader, and a logical extent outside its extended container. The GitHub
Actions run for this update is in progress; do not treat this milestone as passed by CI until
that run reports success.

The parser still is not connected to `mpp_probe`, `mp_decide`, `mpp_apply`, the installer UI,
or GRUB installation. It only reads byte buffers supplied by a caller.


### MBR snapshot-boundary hardening (2026-11-16)

A follow-up review found that the EBR reader accepted a caller-provided `MbrTable`
without independently checking that its extended-container geometry fit the supplied
disk size. The reader now validates the primary-entry count and extents, the extended
partition type and extent, and overlap between the extended container and primary data
partitions before invoking the sector-reader callback.

Regression tests cover a wrong extended type, an extended container beyond the disk,
an impossible primary-entry count, and overlap with a primary partition. GitHub Actions
run 43961078268 completed successfully: strict compilation and the Address/UndefinedBehavior
sanitizer test job both passed.

This remains read-only parser hardening. The installer still uses its existing GPT-only
snapshot and `sgdisk` write path. MBR discovery is now reported separately by `install-disks`,
but it is not used to authorize or perform any disk-writing operation.


## Common read-only disk snapshot milestone (2026-11-16)

Added `disk_snapshot.h/.c`, a pure tagged adapter that converts validated GPT
(`FspTable`) and DOS/MBR (`MbrTable`) parser results into one bounded
`DiskSnapshot` representation. It preserves partition numbering, inclusive extents,
DOS type codes, logical-partition identity, and the extended-container extent.
The adapter rejects duplicate numbers, overlapping extents, invalid bounds, and
unrepresentable input. DOS snapshots are currently restricted to 512-byte logical
sectors; 4Kn DOS layouts are refused conservatively until their LBA semantics are
verified against real hardware and tooling.

Added `disk_snapshot_test.c` and wired it into `make test` and the focused GitHub
Actions workflow. The first expanded CI attempt passed the MBR parser tests but failed before compiling the adapter because the workflow shell continuation was over-escaped. The workflow command was corrected to a single line. The corrected run 43961842490 completed successfully, including strict compilation and sanitizer-backed tests for both the parser and snapshot adapter.

**Scope boundary:** this is a read-only conversion layer only. `FpSnap`, `fp_probe`,
`mpp_probe`, `mp_decide`, `mpp_apply`, UI data contracts, and bootloader installation
remain GPT/UEFI-oriented. The new common representation is not yet used to authorize
or perform any installer operation. MBR installation remains disabled.


## BIOS/MBR manual partition-selection validator (2026-11-16)

Added a separate, read-only validation layer in `mbr_manual.h/.c` for the next
manual-install path. It accepts an existing DOS/MBR partition selected as `/`,
with optional existing `/home` and Linux swap partitions. It distinguishes
primary and logical partition numbers without renumbering them, requires BIOS
boot mode, rejects a busy disk, checks Linux partition type codes and minimum
sizes, prevents assigning one partition to multiple roles, and refuses an
unknown `/home` filesystem when the user chooses to preserve it. Root formatting
must be explicitly selected in the plan; swap partition and swapfile are mutually
exclusive.

The validator deliberately does not create/delete partitions, format filesystems,
mount anything, install GRUB, or write an MBR/EBR. It is not yet connected to
the installer UI or `install-run`. The current GPT manual-install route remains
unchanged. The MBR route is BIOS-only at this stage; UEFI-on-MBR remains gated
until ESP discovery and firmware behavior can be proven.

Tests were added in `server/tests/mbr_manual_test.c` and included in `make test`
and the isolated GitHub Actions workflow. GitHub Actions run 43964488361 passed
the parser, snapshot adapter, manual-selection validator, and disk-probe suites.


## Manual partitioning: current installer boundary

The existing wizard already has a manual partitioning screen for healthy GPT
disks booted in UEFI mode. It lists partitions and lets the user assign
existing partitions to `/`, `/home`, swap, and EFI, or create/delete partitions
inside free space. That path is implemented by `apps/installer/manualmodel.js`,
`server/src/install/manual.c`, and `manualpart.c`.

This is not yet the requested Legacy BIOS + DOS/MBR manual path. The existing
GPT manual model requires an EFI System Partition and routes table changes
through `sgdisk`; it must not be reused for DOS/MBR. The new MBR manual
validator is still a read-only policy component and is not exposed as an
installable UI mode. A separate MBR partition discovery/tagging path, UI model,
backend revalidation, formatting/mount flow, and a proven BIOS GRUB strategy
remain required. Until those exist, the installer must not offer MBR manual
installation as if it were ready.

The MBR validator now independently rechecks snapshot geometry, unique
partition numbers, non-overlapping extents, primary/logical numbering,
extended-container containment, and strict boolean flags before checking the
selected root, home, and swap partitions.


## Read-only MBR/EBR device probe (2026-11-16)

Added `mbr_disk.c/.h`. The probe accepts only an already-open read-only file
descriptor, refuses non-512-byte logical sectors, reads the MBR and follows
the validated EBR chain using bounded `pread` calls, and emits a change-detection
fingerprint over the MBR and every traversed EBR sector. The fingerprint is
FNV-1a 64-bit plus the disk sector count; it is explicitly a change detector,
not a cryptographic authentication mechanism.

A temporary-image test now exercises a primary partition plus an extended
partition with one logical partition, confirms the descriptor is read-only,
checks that no bytes were written, converts the parsed table to `DiskSnapshot`,
and proves the fingerprint changes when an EBR entry changes.

GitHub Actions run 43964488361 passed all four isolated suites: MBR parser,
common snapshot adapter, MBR manual-selection validator, and MBR disk-probe /
snapshot-conversion tests. Each compiled with `-Wall -Wextra -Werror` and
Address/UndefinedBehavior sanitizers. The new source files are also included in
the server and installer test source lists.

The read-only probe is now called by `install-disks` for each candidate disk.
When a pure DOS/MBR table is valid, the response includes `mbrProbeOk`, an MBR/EBR
fingerprint, and `mbrParts` containing partition number, start/end LBA, DOS type,
and primary/logical identity. The probe opens the device read-only and rejects
4Kn logical-sector layouts. The installer integration target compiled successfully
in GitHub Actions run 43964999046 with `SESSIOND=0` to bypass unrelated optional
remote-desktop build dependencies.

The wizard does not yet consume these fields. The MBR-specific selection UI,
backend plan revalidation, formatting/mounting, fstab generation, and a proven
BIOS GRUB strategy are still outstanding. Therefore Legacy BIOS/MBR manual
installation is not yet enabled; the existing GPT/UEFI manual path is unchanged.


## Separate MBR manual-selection UI model (2026-11-17)

Added `apps/installer/mbrmanualmodel.js`, intentionally separate from the existing
GPT model. It consumes only the read-only `mbrProbeOk`, `mbrTag`, and `mbrParts`
snapshot from `install-disks`; it maps filesystem types from the existing lsblk
partition list, offers Linux data partitions (DOS type 0x83) for root/home and
Linux swap (0x82) for swap, and preserves primary/logical partition numbers.
It checks minimum sizes, duplicate role assignments, supported preserved /home
filesystems, and refuses missing probe tags or non-512-byte sectors. Its plan
shape is reserved as `manual-mbr` and includes the probe fingerprint.

The isolated model test is `tests/installer_mbr_manual_model_test.mjs` and is
now part of the MBR GitHub Actions workflow. This is a model/test milestone only:
The wizard does not yet expose it. The backend plan parser now recognizes the reserved `manual-mbr` fields (`mbrTag`, root/home/swap partition numbers, and root/home format flags), and the plan test verifies those values. This is deliberately parser-only: `validate()` still rejects `manual-mbr` as `install.badMode`, so the request cannot enter any disk-writing path. The `rootFormat` field is considered explicit only when its JSON value is the boolean `true` or `false`; `null` and string values do not satisfy that requirement. No partition is formatted, mounted, or modified by this change. The next required work is a fresh read-only probe/tag comparison in `install-run`, followed by a separately tested mount/fstab path and BIOS GRUB backup/rollback strategy. The MBR manual install option must remain hidden until
those protections are complete.

The read-only probe now also exposes `mbr_fingerprint_matches()`, a strict
format-and-equality check for the `install-disks` token and a fresh probe result.
Its tests cover equal tokens, changed geometry/content, empty and malformed
values. This is only a stale-snapshot comparison primitive; it is not wired into
`install-run`, and the FNV token is not cryptographic authentication. A separate pure adapter, `mm_plan_from_fields()`, now converts the parsed root/home/swap values into the MBR policy structure with range checks, rejects omitted or false `rootFormat`, and prevents simultaneous swap-partition and swap-file selections. It is covered by the MBR validator tests but is not yet called by `install-run`. The MBR
manual path remains rejected before any write operation.


## BIOS GRUB shared-install blocker review (2026-11-17)

A source-level review of `server/src/install/install.c` confirms that the existing
`grub-install --target=i386-pc <disk>` call is inside the non-shared (erase)
bootloader branch only. The shared-install branch is currently EFI-only: it
installs an EFI loader and creates an NVRAM entry with `efibootmgr`. Therefore
the existing shared/manual path cannot simply be reused for Legacy BIOS.

Before a BIOS/MBR manual mode can write boot code, it must independently prove:
the target is a pure DOS/MBR disk (not hybrid/protective), the BIOS is the active
boot mode, the post-MBR embedding gap is large enough for the selected GRUB
core image, the disk is not mounted/in use, and the selected partitions still
match the current MBR/EBR fingerprint. A first-partition-at-1MiB heuristic is
not sufficient evidence by itself.

The implementation will need a durable backup of sector 0 (including disk
signature and partition table bytes) and the exact embedding sectors GRUB may
overwrite, with byte-level verification that partition entries remain intact.
Failure after writing boot code must attempt a tested rollback and report
whether restoration succeeded. Existing boot managers may be replaced; this
must be an explicit user confirmation, not described as harmless. Until a
disposable-image/VM test proves backup, install, verify, and rollback behavior,
the MBR manual mode remains unavailable. No GRUB write or real-disk boot-code
change was made in this milestone.


### MBR sector-size field correction

The installer disk JSON now exposes `mbrLsec` from the actual sysfs logical-sector
size used by the read-only MBR probe. The separate MBR selection model uses this
field for both its 512-byte support gate and partition-size calculations; it no
longer reuses `lsec`, which belongs to the GPT/manual-partition probe and may be
zero when GPT probing rejects a DOS disk. The MBR model remains hidden from the
wizard and cannot authorize installation.


## Read-only MBR preflight milestone (2026-11-17)

Added `mbr_preflight.c/.h` as an isolated, read-only last-moment gate for a future BIOS/MBR manual path. Given an already-open read-only disk descriptor, geometry, the install-disks fingerprint and a parsed selection, it:

- re-reads the MBR and complete EBR chain;
- rejects invalid or unsupported layouts and stale fingerprints;
- converts the fresh result to a `DiskSnapshot`;
- runs the server-side `mm_decide` policy with explicit BIOS and busy facts and filesystem facts;
- returns a stable installer error code without formatting, mounting, changing partition tables or installing GRUB.

This helper is not yet called by `install-run`. The `manual-mbr` mode remains rejected by `validate()`, and no MBR write path is enabled. The fingerprint remains a non-cryptographic change detector. The next gate is to integrate this check into a separately branched, still-write-disabled install-run path, then design and test filesystem/mount rollback and BIOS GRUB embedding/backup behavior on disposable disk images and VMs.


The `install-run` validator now returns the explicit `install.manual.mbr.notReady` refusal for `mode: "manual-mbr"` before generic mode dispatch. This makes the fail-closed state observable and prevents accidental fall-through to the erase branch. A plan parser or read-only preflight pass cannot authorize writes.


## Read-only MBR filesystem and mount-plan model

The compatibility branch now contains `server/src/install/mbr_mount_plan.c` and
`mbr_mount_plan.h`. `mp_build()` creates an inspectable ordered plan for an
already-existing DOS/MBR layout: format root as ext4, mount root, optionally
format or preserve /home, optionally initialize a swap partition or describe a
swapfile, and produce fstab lines.

This is strictly a pure planning layer. It does not open block devices, run
mkfs/mkswap, create files, mount filesystems, or write fstab. It reuses the
server-side MBR selection policy, including BIOS mode, busy-disk rejection,
partition identity/type/size checks, and filesystem allowlisting.

The root and newly initialized filesystem UUIDs are represented by explicit
placeholders (`@ROOT_UUID_AFTER_FORMAT@`, `@HOME_UUID_AFTER_FORMAT@`,
`@SWAP_UUID_AFTER_MKSWAP@`). A future executor must replace them only after
the corresponding filesystem operation succeeds and UUID discovery is
verified. Preserved /home requires a valid existing UUID. No device path such
as /dev/sda is persisted into fstab.

This milestone does not authorize installation writes and is not connected to
`install-run`. BIOS GRUB installation, embedding-gap verification, backups,
rollback, mount/busy detection, and end-to-end disposable-image tests remain
blocking work.


## Read-only MBR target-use discovery

The installer disk-discovery JSON now reports `mbrBusy` and a `busy` flag on
each MBR partition. The check inspects each candidate block device and every partition in the
current MBR snapshot using mountinfo device numbers, mount source names for
Btrfs-style anonymous device numbers, sysfs holders, and /proc/swaps. Missing
or unreadable kernel state is treated as busy. This is not yet a complete
ancestry proof for every stacked or multi-device storage topology; the final
privileged preflight must add explicit parent/holder graph traversal and live
medium identity checks before any write path can be enabled.

These values are advisory discovery data only. They are not authorization to
write: the privileged install-run path must repeat the checks immediately
before any future operation, using the fresh disk identity and partition
snapshot. This change does not enable the MBR UI or connect manual-mbr to an
installer execution branch.


### MBR busy-state probe details

`mbr_busy.c` now provides a separately tested, fail-closed check. It requires
readable sysfs device numbers, `/proc/self/mountinfo`, `/proc/swaps`, and
each block device's sysfs `holders` directory. It detects direct mount major/minor
matches, Btrfs mounts whose mountinfo device number is anonymous but whose
source names the partition, active swap devices, and active holders such as
device-mapper/LVM/MD. Missing or malformed state is treated as busy.

The probe is read-only and supports a fake sysfs/proc root for tests. It is
currently used only to annotate discovery JSON (`mbrBusy` and per-partition
`busy`); the privileged execution path still refuses `manual-mbr`. A future
installer preflight must call it again immediately before any action and
revalidate the disk fingerprint.


## BIOS GRUB post-MBR gap geometry (read-only)

`mbr_grub_gap.c` computes the potential embedding area between the MBR sector
(LBA 0) and the earliest primary partition or extended-partition container.
It compares that sector count with the exact GRUB core-image byte size supplied
by a future caller. It does not assume a 1 MiB alignment.

This is only a partition-table geometry calculation. It cannot prove the gap
contains no previous bootloader, metadata, or user data, and it does not model
every sector GRUB may write. It performs no backup, no GRUB invocation, and no
disk write. A successful result is not authorization to install GRUB. Exact
core-image sizing, gap ownership verification, backups, rollback, and disposable
disk-image tests remain mandatory blockers.


### Busy-probe regression coverage

The busy-state unit tests also exercise per-partition reporting, anonymous
mountinfo source matching, active swap, active sysfs holders, and rejection of
non-MBR snapshots. The fake-tree tests do not emulate every kernel storage
topology; they are not a substitute for disposable VM tests with dm-crypt,
LVM, MD RAID, Btrfs multi-device, USB live media, and mounted root filesystems.


## BIOS GRUB replacement and backup preflight model (planning only)

The branch now has `mbr_grub_guard.c` and `mbr_grub_guard.h`. Its pure
planner requires BIOS boot, a disk reported not busy, matching expected and
fresh MBR fingerprints, explicit acknowledgement that an existing boot manager
may be replaced, and enough geometrically available post-MBR sectors for the
caller-supplied exact core-image size.

On success it proposes backing up LBA 0 through the end of the candidate
pre-partition gap, and identifies LBA 1 onward as the candidate embedding
region. The plan explicitly reports `writes_authorized = 0`. It does not read
or save those sectors, prove they are unused, invoke `grub-install`, or verify
which sectors a particular GRUB build will actually modify. Therefore it is
not yet safe to execute and is not connected to `install-run`.

Before any bootloader write path can be enabled, a separate implementation
must establish the exact GRUB write set for the shipped GRUB version, verify
the full gap contents and ownership (including prior bootloader metadata),
persist and verify a byte-exact backup, install only on disposable disk images,
verify the partition table and disk signature are unchanged, and test rollback
after simulated interruption. Real disks remain out of scope until all those
tests pass.


## MBR UI busy-state gating

The isolated MBR selection model now hides every partition when the disk-level
`mbrBusy` flag is true and excludes individual partitions marked `busy`.
Reviewing a selected busy disk or partition returns the explicit
`install.manual.mbr.busy` error and never produces a backend-ready plan.
This is only a UI-side guard; the server remains authoritative and the
`manual-mbr` execution mode remains disabled.
