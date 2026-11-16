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
