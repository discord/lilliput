#!/bin/sh

set -eu

main() {
  symlink="$1"
  target="$(readlink -f "$symlink")"

  rm "$symlink"
  cp "$target" "$symlink"
}

main "$1"
