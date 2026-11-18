#!/bin/sh
# Fail closed unless the installed BIOS GRUB binaries and module set match the
# exact Debian 13 build audited for the current MBR embedding write-set.
set -eu

EXPECTED_VERSION='2.12-9+deb13u2'
EXPECTED_GRUB_INSTALL_SHA256='3ba6798ca720c6e1b574eb7227b59da586587111c253d3af566cab3f5233daa5'
EXPECTED_GRUB_BIOS_SETUP_SHA256='083fc5c0f9d45b3ff807bb854696333121950ba030c48f61161b0e51fa42297c'
EXPECTED_MODULES_SHA256='e280476c764ae75f06cd50604534de5f96b8334c51a61ef1d6c9e5cb24484b38'

for package in grub-common grub2-common grub-pc-bin; do
    actual=$(dpkg-query -W -f='${Version}' "$package")
    [ "$actual" = "$EXPECTED_VERSION" ] || {
        echo "Unsupported $package version: $actual (audited: $EXPECTED_VERSION)" >&2
        exit 1
    }
done

[ "$(grub-install --version)" = "grub-install (GRUB) $EXPECTED_VERSION" ] || {
    echo "grub-install version string does not match audited version" >&2
    exit 1
}
[ "$(/usr/lib/grub/i386-pc/grub-bios-setup --version)" = "/usr/lib/grub/i386-pc/grub-bios-setup (GRUB) $EXPECTED_VERSION" ] || {
    echo "grub-bios-setup version string does not match audited version" >&2
    exit 1
}

grub_install_hash=$(sha256sum /usr/sbin/grub-install | awk '{print $1}')
grub_bios_setup_hash=$(sha256sum /usr/lib/grub/i386-pc/grub-bios-setup | awk '{print $1}')
modules_hash=$(find /usr/lib/grub/i386-pc -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | awk '{print $1}')

[ "$grub_install_hash" = "$EXPECTED_GRUB_INSTALL_SHA256" ] || {
    echo "grub-install binary hash differs from audited build" >&2
    exit 1
}
[ "$grub_bios_setup_hash" = "$EXPECTED_GRUB_BIOS_SETUP_SHA256" ] || {
    echo "grub-bios-setup binary hash differs from audited build" >&2
    exit 1
}
[ "$modules_hash" = "$EXPECTED_MODULES_SHA256" ] || {
    echo "i386-pc GRUB module set differs from audited build" >&2
    exit 1
}

install -d -m 0755 /usr/share/lordos
cat > /usr/share/lordos/grub-mbr-write-set.txt <<EOF
format=1
audit_run=44364764781
audit_commit=08c4edec2789621eea074a05059d54e89c6f4f84
audit_artifact=14455798492
debian_suite=trixie
architecture=amd64
grub_package_version=$EXPECTED_VERSION
grub_install_sha256=$grub_install_hash
grub_bios_setup_sha256=$grub_bios_setup_hash
i386_pc_modules_sha256=$modules_hash
logical_sector_size=512
write_offset_bytes=0
write_length_bytes=58880
write_last_lba=114
EOF

echo "BIOS GRUB write-set verified: $EXPECTED_VERSION, bytes 0..58879"
