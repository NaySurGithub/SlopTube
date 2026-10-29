#!/bin/sh
# Cuts a release: sets the project version, commits, tags "v<version>" and pushes both. CI then
# builds the APK signed with the release key and publishes it (see docs/releasing.md).
#
# Usage: tools/release.sh <major.minor.patch>
set -eu

cd "$(dirname "$0")/.."

version="${1:-}"
if ! printf '%s' "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "usage: tools/release.sh <major.minor.patch>" >&2
    exit 1
fi
# The version code packs minor and patch into two digits each (cmake/AndroidPackaging.cmake).
minor=$(echo "$version" | cut -d. -f2)
patch=$(echo "$version" | cut -d. -f3)
if [ "$minor" -gt 99 ] || [ "$patch" -gt 99 ]; then
    echo "minor and patch must stay below 100" >&2
    exit 1
fi

current=$(sed -nE 's/^ *VERSION ([0-9]+\.[0-9]+\.[0-9]+)$/\1/p' CMakeLists.txt)
newest=$(printf '%s\n%s\n' "$current" "$version" | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)
if [ "$version" = "$current" ] && git rev-parse -q --verify "refs/tags/v$version" >/dev/null; then
    echo "v$version is already released" >&2
    exit 1
fi
if [ "$newest" != "$version" ]; then
    echo "$version is older than the current version $current" >&2
    exit 1
fi

if [ "$(git branch --show-current)" != main ]; then
    echo "releases are cut from main" >&2
    exit 1
fi
if [ -n "$(git status --porcelain)" ]; then
    echo "the working tree has uncommitted changes" >&2
    exit 1
fi
git fetch -q origin main
if [ "$(git rev-parse HEAD)" != "$(git rev-parse origin/main)" ]; then
    echo "main differs from origin/main; pull or push first" >&2
    exit 1
fi

if [ "$version" != "$current" ]; then
    sed -i.bak -E "s/^( *VERSION )$current\$/\1$version/" CMakeLists.txt
    rm CMakeLists.txt.bak
    git commit -q -m "release: v$version" CMakeLists.txt
fi
git tag -a "v$version" -m "SlopTube $version"
git push -q --atomic origin main "v$version"
echo "Pushed v$version: https://github.com/NaySurGithub/SlopTube/actions"
