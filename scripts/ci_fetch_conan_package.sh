#!/usr/bin/env bash
#
# Fetch a tagged cross-repo Conan cache artifact from a GitHub Release and
# restore it into the current CONAN_HOME. Intended for CI/release jobs; local
# dev normally uses sibling checkouts or /opt/coderoast/conan-stable.
#
# Workflow:
#   1. `gh release download <tag> -R <repo> -p '<pkg>-<ver>.tgz'`
#      pulls the tarball produced by LogCraft's release-publish.yml.
#   2. `conan cache restore <tarball>` injects the recipe + binary into
#      the consumer's CONAN_HOME, identical to `cache save`/`restore`
#      on the local shared cache.
#
# Idempotent: if the package is already in the local cache (same recipe
# revision), this is a no-op and exits 0 without contacting GitHub.
#
# Usage:
#   bash scripts/ci_fetch_conan_package.sh logcraft_core X.X.X CodeRoasted/logcraft
#   bash scripts/ci_fetch_conan_package.sh coderoast_ipc X.X.X CodeRoasted/coderoast-ipc
#
# Requirements:
#   * `gh` CLI on PATH (pre-installed on GitHub-hosted runners).
#   * `GH_TOKEN` env (or `GITHUB_TOKEN`) with read access to the source
#     repo's releases. On GitHub Actions, `secrets.GITHUB_TOKEN` is
#     sufficient when both repos are in the same org and visibility
#     permits; otherwise pass a fine-grained PAT via env.
#   * Same `linux-gcc15-release` profile already present in CONAN_HOME
#     (otherwise the restored binary's settings won't match a consumer
#     install resolving against a different profile sha).

set -euo pipefail

PKG_NAME="${1:-}"
PKG_VERSION="${2:-}"
SOURCE_REPO="${3:-CodeRoasted/logcraft}"
# RELEASE_TAG: the GitHub Release tag that holds the asset. Defaults to
# v${PKG_VERSION}, which is correct for logcraft_core (where the package
# version == the release tag). For packages owned by another repo, pass that
# repo and release tag explicitly as the 3rd and 4th arguments.
RELEASE_TAG="${4:-v${PKG_VERSION}}"

if [[ -z "$PKG_NAME" || -z "$PKG_VERSION" ]]; then
    echo "usage: $0 <pkg-name> <version> [owner/repo] [release-tag]" >&2
    exit 2
fi

PKG_REF="${PKG_NAME}/${PKG_VERSION}"

# Fast path: already vendored.
if conan list "$PKG_REF" --format=compact 2>/dev/null | grep -q "$PKG_REF"; then
    echo "ci_fetch_conan_package: $PKG_REF already in local cache, skipping download."
    exit 0
fi

if ! command -v gh >/dev/null 2>&1; then
    echo "ci_fetch_conan_package: gh CLI not found on PATH" >&2
    exit 1
fi

TAG="$RELEASE_TAG"
ASSET="${PKG_NAME}-${PKG_VERSION}.tgz"
WORKDIR="$(mktemp -d -t ci_fetch_conan_package.XXXXXX)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "ci_fetch_conan_package: downloading $ASSET from $SOURCE_REPO@$TAG"
gh release download "$TAG" \
    --repo "$SOURCE_REPO" \
    --pattern "$ASSET" \
    --dir "$WORKDIR"

TARBALL="$WORKDIR/$ASSET"
[[ -f "$TARBALL" ]] || { echo "ci_fetch_conan_package: $ASSET missing after download" >&2; exit 1; }

echo "ci_fetch_conan_package: restoring $TARBALL into CONAN_HOME=${CONAN_HOME:-default}"
conan cache restore "$TARBALL" >/dev/null

echo "ci_fetch_conan_package: $PKG_REF vendored OK"
