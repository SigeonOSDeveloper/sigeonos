#!/bin/sh
# Ensure the Sigeon OS boot medium is mounted at /run/archiso/bootmnt.
# archiso's initramfs usually mounts it there, but on some hardware/UEFI
# setups it is missing when Calamares runs, which makes unpackfs fail with
# "Bad unpackfs configuration" (source filesystem does not exist).
set -u

target="/run/archiso/bootmnt/sigeonos/x86_64/airootfs.sfs"

if [ -f "$target" ]; then
    exit 0
fi

mkdir -p /run/archiso/bootmnt

# Try the archisosearchuuid passed on the kernel command line.
uuid="$(sed -n 's/.*archisosearchuuid=\([^ ]*\).*/\1/p' /proc/cmdline 2>/dev/null)"
if [ -n "$uuid" ]; then
    dev="$(blkid -U "$uuid" 2>/dev/null)"
    if [ -n "$dev" ] && [ -b "$dev" ]; then
        mount -o ro "$dev" /run/archiso/bootmnt 2>/dev/null
        if [ -f "$target" ]; then
            exit 0
        fi
        umount /run/archiso/bootmnt 2>/dev/null
    fi
fi

# Fallback: probe block devices read-only for the install medium.
for dev in $(lsblk -rno NAME 2>/dev/null | awk '{print "/dev/"$1}'); do
    [ -b "$dev" ] || continue
    mount -o ro "$dev" /run/archiso/bootmnt 2>/dev/null || continue
    if [ -f "$target" ]; then
        exit 0
    fi
    umount /run/archiso/bootmnt 2>/dev/null
done

echo "ERROR: could not find the Sigeon OS boot medium (airootfs.sfs)." >&2
exit 1
