#!/bin/sh
# Remove archiso-only settings from the installed system and give it a
# persistent pacman keyring.
#
# 1) mkinitcpio: the archiso profile ships /etc/mkinitcpio.conf.d/archiso.conf
#    and an archiso linux.preset so the live medium can boot from the ISO. If
#    those are left in place, the installed system's initramfs is built with
#    the archiso hooks, which search for the boot medium and fail to find a
#    root device ("device did not show up after 30 seconds").
#
# 2) pacman keyring: the live system mounts an empty tmpfs over
#    /etc/pacman.d/gnupg and fills it at boot via pacman-init.service. On the
#    installed system that would make the keyring volatile and broken, so
#    drop both units and initialize a real, persistent keyring instead
#    (without it, every pacman -Sy fails: "core.db: signature ... unknown
#    trust").

rm -f /etc/mkinitcpio.conf.d/archiso.conf

rm -f /etc/systemd/system/pacman-init.service \
      /etc/systemd/system/multi-user.target.wants/pacman-init.service \
      /etc/systemd/system/etc-pacman.d-gnupg.mount

# Replace the copied unit files in the .wants dirs (from the profile's
# airootfs) with real symlinks so the installed system always runs the units
# shipped by the installed packages.
ln -sf /usr/lib/systemd/system/NetworkManager.service \
    /etc/systemd/system/multi-user.target.wants/NetworkManager.service
ln -sf /usr/lib/systemd/system/systemd-resolved.service \
    /etc/systemd/system/multi-user.target.wants/systemd-resolved.service
ln -sf /usr/lib/systemd/system/lightdm.service \
    /etc/systemd/system/multi-user.target.wants/lightdm.service

install -Dm644 /usr/share/sigeonos/linux.preset /etc/mkinitcpio.d/linux.preset

# Ship the pacman sync databases that came inside the ISO (archiso's cleanup
# removed /var/lib/pacman/sync before packing the airootfs, so copy them over
# from the stash during install).
if [ -d /usr/share/sigeonos/pacman-sync ]; then
    mkdir -p /var/lib/pacman/sync
    cp -f /usr/share/sigeonos/pacman-sync/*.db /var/lib/pacman/sync/
fi

pacman-key --init
pacman-key --populate archlinux
