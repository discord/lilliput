#!/bin/sh

set -eu

main() {
  symlink="$1"
  if [[ "$OSTYPE" == "darwin"* ]]; then
    # use greadlink from coreutils
    target="$(greadlink -f "$symlink")"
  else
    target="$(readlink -f "$symlink")"
  fi

  rm "$symlink"
  cp "$target" "$symlink"
}

main "$1"
