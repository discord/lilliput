#!/bin/sh
set -eu

symlink="$1"
case "$(uname)" in
    osx*)
        if command -v greadlink > /dev/null; then
            target="$(greadlink -f "$symlink")"
        else
            echo "Please install coreutils"
            exit 1
        fi
        ;;
    *)
        target="$(readlink -f "$symlink")"
        ;;
esac

rm "$symlink"
cp "$target" "$symlink"
