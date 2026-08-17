# Sigeon OS

A lightweight Arch-based Linux distribution with an XFCE desktop, built with [archiso](https://gitlab.archlinux.org/archlinux/archiso).

## Download

The easiest way to get Sigeon OS is to download the latest ISO from the [official website](https://sigeonosdeveloper.github.io/sigeonos/) — available from Google Drive and GoFile mirrors, with the SHA-256 checksum listed there.

## Features

- **Sigeon Hello** - Welcome app with quick actions: system updates, drivers, terminal, DaVinci Resolve installer, and app store. Auto-launches on login (toggleable).
- **Sigeon Store** - A Flathub GUI store: browse apps with icons, search, and one-click installs via Flatpak.
- **Sigeon Updater** - System update manager.
- **Sigeon Drivers** - GPU, audio, and printer driver installer.
- **DaVinci Resolve installer** - One-click AppImage-based install.
- **SigeonOS installer** - Calamares-based graphical installer with custom branding.

## Building the ISO (optional)

Prefer to build it yourself instead of downloading? Building the ISO is a secondary option for developers and tinkerers.

### Requirements

- Arch Linux (or Arch-based) host to build the ISO
- `archiso`
- `flatpak` (runtime dependency on the target system)

### Build

```bash
# Build the archiso profile
sudo ./scripts/build.sh
```

Output ISO lands in `out/`.

### Building the packages

Packages live in `pkgbuilds/`. To build one or more and add them to the local repo:

```bash
./scripts/build_pkg.sh sigeon-apps sigeonos-branding
```

## Repository layout

```
profile/       archiso build profile (XFCE desktop, packages, branding)
pkgbuilds/     first-party PKGBUILDs (apps, branding, calamares, keyring)
scripts/       build helpers
assets/        shared artwork (logo, wallpaper)
repo/          signed local package repository
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).