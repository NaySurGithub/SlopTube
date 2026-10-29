#!/bin/sh
# One-time setup of the release signing key: creates a keystore with a random password outside
# the repository and stores both as GitHub Actions secrets, which CI reads for "v*" tags.
#
# Keep the keystore and its password safe: Android only installs an update signed with the same
# key, so losing them means users must uninstall to move to a newly signed build.
#
# Usage: tools/release-key.sh [keystore path]
set -eu

keystore="${1:-$HOME/.config/studio.kanye.sloptube/release.keystore}"
password_file="$keystore.password"

if [ -e "$keystore" ]; then
    echo "$keystore already exists; delete it first to make a new key" >&2
    exit 1
fi
command -v keytool >/dev/null || PATH="/opt/homebrew/opt/openjdk@17/bin:$PATH"
command -v gh >/dev/null || {
    echo "the GitHub CLI (gh) is required" >&2
    exit 1
}

mkdir -p "$(dirname "$keystore")"
umask 077
password=$(openssl rand -base64 32 | tr -d '/+=' | cut -c1-32)
printf '%s' "$password" > "$password_file"

keytool -genkeypair -keystore "$keystore" -storetype PKCS12 -alias sloptube \
    -storepass "$password" -keyalg RSA -keysize 4096 -validity 36500 \
    -dname "CN=SlopTube" >/dev/null 2>&1

base64 < "$keystore" | tr -d '\n' | gh secret set SLOPTUBE_KEYSTORE_BASE64
gh secret set SLOPTUBE_KEYSTORE_PASSWORD < "$password_file"

echo "Keystore: $keystore"
echo "Password: $password_file"
echo "Both are stored as GitHub secrets. Back them up somewhere safe."
