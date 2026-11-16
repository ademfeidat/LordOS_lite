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
snapshot and `sgdisk` write path; no MBR table is yet exposed to the installer and no
disk-writing behavior has been enabled.


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

Tests were added in `server/tests/mbr_manual_test.c`, and the test is included
in `make test` and the isolated GitHub Actions workflow. This new test has been
committed but is awaiting its first CI result.


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
