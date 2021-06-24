#!/bin/sh

set -eu

main() {
  symlink="$1"
  if [[ "$OSTYPE" == "darwin"* ]]; then
    if command -v greadlink > /dev/null; then
      target="$(greadlink -f "$symlink")"
    else
      echo "Please install coreutils"
      exit 1
    fi
  else
    target="$(readlink -f "$symlink")"
  fi

  rm "$symlink"
  cp "$target" "$symlink"
}

main "$1"
