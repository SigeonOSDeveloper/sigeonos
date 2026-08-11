#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KEYS="$PROJECT_ROOT/keys"
REPO_DIR="$PROJECT_ROOT/repo"
GPGKEY="6862721407EDD7F103EAE2E3491614EC56D332AD"

usage() {
    echo "usage: $0 <pkgbuild-dir> [pkgbuild-dir ...]"
    exit 1
}

[ $# -ge 1 ] || usage

export GNUPGHOME="$KEYS/gnupg"
export GPGKEY

for dir in "$@"; do
    pkgdir="$PROJECT_ROOT/pkgbuilds/$dir"
    [ -d "$pkgdir" ] || { echo "error: no such pkgbuild dir: $dir" >&2; exit 1; }
    echo "==> Building $dir"
    (cd "$pkgdir" && makepkg --sign -Ccf ${MAKEPKG_ARGS:-})
    echo "==> Moving package to repo"
    mv "$pkgdir"/*.pkg.tar.zst "$pkgdir"/*.pkg.tar.zst.sig "$REPO_DIR/"
done

echo "==> Updating repo database"
repo-add --sign --key "$GPGKEY" "$REPO_DIR/sigeonos.db.tar.zst" "$REPO_DIR"/*.pkg.tar.zst
