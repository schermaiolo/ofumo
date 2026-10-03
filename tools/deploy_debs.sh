#!/usr/bin/env bash
set -Eeuo pipefail

die()
{
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage()
{
    printf 'Usage: %s <version>\n' "$(basename "$0")"
    printf 'Example: %s 0.0.2\n' "$(basename "$0")"
}

[[ $# -eq 1 ]] || {
    usage
    exit 2
}

VERSION="${1#v}"
TAG="v${VERSION}"
ARCH="amd64"
DIST="trixie"
IMAGE="ofumo-deb-builder:${DIST}"

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" ||
    die "run this script from inside the OFumo repository"
cd "$ROOT"

command -v docker >/dev/null || die "docker is required"
git diff --quiet || die "working tree has unstaged changes"
git diff --cached --quiet || die "index has staged but uncommitted changes"
git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null ||
    die "tag ${TAG} does not exist"
if git show-ref --verify --quiet refs/heads/debian/main; then
    DEBIAN_REF="debian/main"
elif git show-ref --verify --quiet refs/remotes/origin/debian/main; then
    DEBIAN_REF="origin/debian/main"
else
    die "Debian packaging branch debian/main was not found"
fi

git merge-base --is-ancestor "${TAG}" "${DEBIAN_REF}" ||
    die "${DEBIAN_REF} does not contain ${TAG}"

CMAKE_VERSION="$(
    git show "${TAG}:CMakeLists.txt" |
    sed -nE 's/.*project\([^)]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' |
    head -n1
)"
[[ "$CMAKE_VERSION" == "$VERSION" ]] ||
    die "CMake version at ${TAG} is '${CMAKE_VERSION:-unknown}', expected ${VERSION}"

PKG_VERSION="$(
    git show "${DEBIAN_REF}:debian/changelog" |
    sed -nE '1s/^ofumo \(([^)]+)\).*/\1/p'
)"
[[ "$PKG_VERSION" == "${VERSION}-"* ]] ||
    die "debian/main changelog version '${PKG_VERSION:-unknown}' does not match ${VERSION}-N"

OUT="${ROOT}/deploy/${TAG}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ofumo-deploy.XXXXXX")"

cleanup()
{
    rm -rf "$WORK"
}
trap cleanup EXIT

printf '==> OFumo %s / Debian %s\n' "$VERSION" "$PKG_VERSION"
printf '==> Temporary workspace: %s\n' "$WORK"
printf '==> Final output: %s\n' "$OUT"

rm -rf "$OUT"
mkdir -p "$OUT" "$WORK/ofumo-${VERSION}"

printf '==> Exporting pristine upstream tag %s\n' "$TAG"
git archive \
    --format=tar.gz \
    --prefix="ofumo-${VERSION}/" \
    -o "$WORK/ofumo-${VERSION}.tar.gz" \
    "$TAG"

cp "$WORK/ofumo-${VERSION}.tar.gz" \
   "$WORK/ofumo_${VERSION}.orig.tar.gz"

printf '==> Exporting Debian packaging branch\n'
git archive "${DEBIAN_REF}" | tar -x -C "$WORK/ofumo-${VERSION}"

printf '==> Building reusable Debian builder image\n'
docker build \
    -t "$IMAGE" \
    -f packaging/debian/Dockerfile \
    .

printf '==> Creating Debian source package\n'
docker run --rm \
    -e VERSION="$VERSION" \
    -v "$WORK:/work" \
    "$IMAGE" \
    bash -lc '
        set -Eeuo pipefail
        cd "/work/ofumo-${VERSION}"
        dpkg-checkbuilddeps
        dpkg-buildpackage -S -us -uc
    '

DSC="$WORK/ofumo_${PKG_VERSION}.dsc"
[[ -f "$DSC" ]] || die "source package was not produced: $DSC"

printf '==> Running clean sbuild against Debian %s\n' "$DIST"
docker run --rm --privileged \
    -e PKG_VERSION="$PKG_VERSION" \
    -e DIST="$DIST" \
    -v "$WORK:/work" \
    "$IMAGE" \
    bash -lc '
        set -Eeuo pipefail
        sbuild-createchroot \
            "$DIST" \
            "/srv/chroot/${DIST}-amd64-sbuild" \
            https://deb.debian.org/debian

        cd /work
        sbuild \
            -d "$DIST" \
            -c "${DIST}-amd64-sbuild" \
            "ofumo_${PKG_VERSION}.dsc"
    '

DEB="$WORK/ofumo_${PKG_VERSION}_${ARCH}.deb"
DBG="$WORK/ofumo-dbgsym_${PKG_VERSION}_${ARCH}.deb"

[[ -f "$DEB" ]] || die "sbuild did not produce $DEB"
[[ -f "$DBG" ]] || die "sbuild did not produce $DBG"

printf '==> Smoke-testing package installation\n'
docker run --rm \
    -e VERSION="$VERSION" \
    -e PKG_VERSION="$PKG_VERSION" \
    -v "$WORK:/pkg:ro" \
    debian:13 \
    bash -lc '
        set -Eeuo pipefail
        apt-get update >/dev/null
        apt-get install -y "/pkg/ofumo_${PKG_VERSION}_amd64.deb" >/dev/null

        test "$(ofumo --version)" = "ofumo ${VERSION}"
        dpkg -L ofumo | grep -qx "/usr/bin/ofumo"
        dpkg -L ofumo | grep -qx "/usr/share/man/man1/ofumo.1.gz"
        dpkg -L ofumo | grep -qx "/usr/share/doc/ofumo/copyright"
    '

printf '==> Collecting release artifacts\n'
cp "$WORK/ofumo-${VERSION}.tar.gz" "$OUT/"
cp "$WORK/ofumo_${VERSION}.orig.tar.gz" "$OUT/"
cp "$WORK/ofumo_${PKG_VERSION}.dsc" "$OUT/"
cp "$WORK/ofumo_${PKG_VERSION}.debian.tar.xz" "$OUT/"
cp "$DEB" "$OUT/"
cp "$DBG" "$OUT/"

for suffix in \
    "_source.buildinfo" \
    "_source.changes" \
    "_${ARCH}.buildinfo" \
    "_${ARCH}.changes"
do
    file="$WORK/ofumo_${PKG_VERSION}${suffix}"
    [[ -f "$file" ]] && cp "$file" "$OUT/"
done

build_log="$(
    find "$WORK" -maxdepth 1 -type f \
        -name "ofumo_${PKG_VERSION}_${ARCH}-*.build" \
        -print -quit
)"
[[ -n "$build_log" ]] && cp "$build_log" "$OUT/"

(
    cd "$OUT"
    sha256sum \
        "ofumo_${PKG_VERSION}_${ARCH}.deb" \
        "ofumo-dbgsym_${PKG_VERSION}_${ARCH}.deb" \
        > SHA256SUMS
)

printf '\nOFumo %s release candidate is ready.\n\n' "$VERSION"
printf 'Public release assets:\n'
printf '  %s\n' \
    "$OUT/ofumo_${PKG_VERSION}_${ARCH}.deb" \
    "$OUT/ofumo-dbgsym_${PKG_VERSION}_${ARCH}.deb" \
    "$OUT/SHA256SUMS"
printf '\nNothing was pushed or published.\n'
