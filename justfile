# pithddu-firmware tasks (Rust / esp-idf). Run `just` to list.
# Source the esp env first:  source ~/export-esp.sh
set shell := ["bash", "-uc"]

default:
    @just --list

# Build the firmware (debug).
build:
    cargo build

# Build optimized.
release-build:
    cargo build --release

# Flash + monitor over USB (espflash). PORT defaults to autodetect.
flash port="":
    cargo build --release
    espflash flash --monitor {{ if port != "" { "-p " + port } else { "" } }} \
        target/xtensa-esp32s3-espidf/release/pithddu

# Serial monitor only.
monitor port="":
    espflash monitor {{ if port != "" { "-p " + port } else { "" } }}

# Host unit tests for the pure-logic core.
test:
    cargo +stable test -p pith-core --target x86_64-unknown-linux-gnu

# Save the bare app image (what the dashboard installs by board name).
image:
    cargo build --release
    espflash save-image --chip esp32s3 \
        target/xtensa-esp32s3-espidf/release/pithddu pithddu-xiao_s3.bin

# Cut a release by tagging + pushing (CI builds the bins and publishes the GitHub Release).
#   just release           -> bump the patch of the latest vX.Y.Z tag (0.1.0 if none)
#   just release 1.2.3     -> release exactly that version
release version="":
    #!/usr/bin/env bash
    set -euo pipefail
    git fetch --tags --quiet
    ver="{{version}}"
    if [ -z "$ver" ]; then
        last=$(git tag -l 'v*' --sort=-v:refname | head -n1)
        if [ -z "$last" ]; then
            ver="0.1.0"
        else
            base=${last#v}
            IFS='.' read -r MA MI PA <<<"$base"
            ver="${MA:-0}.${MI:-0}.$(( ${PA:-0} + 1 ))"
        fi
    fi
    ver=${ver#v}
    tag="v${ver}"
    if git rev-parse "$tag" >/dev/null 2>&1; then
        echo "tag $tag already exists — pick another version" >&2
        exit 1
    fi
    branch=$(git rev-parse --abbrev-ref HEAD)
    echo "Releasing $tag from $branch @ $(git rev-parse --short HEAD)"
    git tag -a "$tag" -m "release $tag"
    git push origin "$tag"
    echo "Pushed $tag — GitHub Actions will build and publish the release."
