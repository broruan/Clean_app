#!/usr/bin/env bash
# Build on the target Ubuntu release and CPU architecture; no cross-distro binaries.
set -euo pipefail
umask 022

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="${project_dir}/dist"
qt_major=5
jobs="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

usage() {
    cat <<'USAGE'
Usage: bash packaging/build-deb.sh [--output-dir DIR] [--qt-major 5|6] [--jobs N]

Build, test and verify a native Ubuntu .deb in a fresh temporary build directory.
Default: Qt 5, four parallel jobs, packages written to ./dist.
Ubuntu 22.04 / 24.04 / 26.04; amd64 / arm64.
USAGE
}

while (($#)); do
    case "$1" in
        --output-dir|--qt-major|--jobs)
            (($# >= 2)) || { echo "Missing value for $1" >&2; exit 2; }
            case "$1" in
                --output-dir) output_dir="$2" ;;
                --qt-major) qt_major="$2" ;;
                --jobs) jobs="$2" ;;
            esac
            shift 2
            ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done
[[ "$qt_major" =~ ^[56]$ ]] || { echo '--qt-major must be 5 or 6' >&2; exit 2; }
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo '--jobs must be a positive integer' >&2; exit 2; }

for tool in cmake ctest dpkg dpkg-deb dpkg-shlibdeps readelf gzip desktop-file-validate xmllint; do
    command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 1; }
done
# /etc/os-release belongs to the build system, not the source checkout.
source /etc/os-release
[[ "${ID:-}" == ubuntu ]] || { echo 'Build this package on Ubuntu.' >&2; exit 1; }
case "${VERSION_ID:-}" in
    22.04|24.04|26.04) ubuntu_version="$VERSION_ID" ;;
    *) echo "Unsupported Ubuntu release: ${VERSION_ID:-unknown}" >&2; exit 1 ;;
esac
package_arch="$(dpkg --print-architecture)"
case "$package_arch" in
    amd64|arm64) ;;
    *) echo "Unsupported package architecture: $package_arch" >&2; exit 1 ;;
esac

mkdir -p -- "$output_dir" "${project_dir}/build"
output_dir="$(cd -- "$output_dir" && pwd)"
# A fresh tree prevents stale Qt, host, architecture or CMake cache reuse.
work_dir="$(mktemp -d "${project_dir}/build/deb-${ubuntu_version}-${package_arch}.XXXXXX")"
trap 'rm -rf -- "$work_dir"' EXIT
build_dir="${work_dir}/build"
stage_dir="${work_dir}/stage"
dependency_dir="${work_dir}/dependency-check"

cmake -S "$project_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DDISKLENS_QT_MAJOR="$qt_major"
cmake --build "$build_dir" --parallel "$jobs"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir" --output-on-failure
package_version="$(cat "$build_dir/disklens-version.txt")"
[[ "$package_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
    echo "Invalid CMake project version: $package_version" >&2; exit 1;
}

mkdir -p "$stage_dir/DEBIAN" "$dependency_dir/debian"
DESTDIR="$stage_dir" cmake --install "$build_dir" --prefix /usr --strip
cat > "$dependency_dir/debian/control" <<'CONTROL'
Source: disklens
Section: utils
Priority: optional
Maintainer: broruan <broruan@users.noreply.github.com>

Package: disklens
Architecture: any
Description: Local disk usage and installed software explorer
CONTROL
package_depends="$(cd "$dependency_dir" && dpkg-shlibdeps -O "$stage_dir/usr/bin/disklens" | sed -n 's/^shlibs:Depends=//p')"
[[ -n "$package_depends" ]] || { echo 'Could not determine runtime dependencies.' >&2; exit 1; }
# Qt loads SVG support dynamically, outside the ELF dependency graph.
svg_dependency=libqt5svg5
if [[ "$qt_major" == 6 ]]; then
    svg_dependency=libqt6svg6
    # Qt 6.4+ distributions split the image/icon plugins out of libqt6svg6.
    if dpkg-query -W -f='${Status}' qt6-svg-plugins 2>/dev/null | grep -qx 'install ok installed'; then
        svg_dependency+=', qt6-svg-plugins'
    elif [[ "$ubuntu_version" != 22.04 ]]; then
        echo 'Install qt6-svg-plugins before packaging with Qt 6.' >&2; exit 1
    fi
fi
for runtime_package in "libqt${qt_major}svg${qt_major}" pkexec xdg-utils; do
    dpkg-query -W -f='${Status}' "$runtime_package" 2>/dev/null | grep -qx 'install ok installed' || {
        echo "Install the runtime dependency before packaging: $runtime_package" >&2; exit 1;
    }
done

source_epoch="${SOURCE_DATE_EPOCH:-}"
if [[ -z "$source_epoch" ]] && git -C "$project_dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    source_epoch="$(git -C "$project_dir" log -1 --format=%ct 2>/dev/null || true)"
fi
source_epoch="${source_epoch:-$(date +%s)}"
[[ "$source_epoch" =~ ^[0-9]+$ ]] || { echo 'SOURCE_DATE_EPOCH must be an integer.' >&2; exit 1; }
export SOURCE_DATE_EPOCH="$source_epoch"
changelog_date="$(LC_ALL=C date -u -d "@${source_epoch}" -R)"
cat > "$stage_dir/usr/share/doc/disklens/changelog" <<CHANGELOG
disklens (${package_version}) ${VERSION_CODENAME}; urgency=medium

  * Native Ubuntu ${ubuntu_version} ${package_arch} build of DiskLens.
  * See upstream release notes for changes and validation boundaries.

 -- broruan <broruan@users.noreply.github.com>  ${changelog_date}
CHANGELOG
gzip -n -9 "$stage_dir/usr/share/doc/disklens/changelog"
installed_kib="$(du -sk "$stage_dir/usr" | cut -f1)"
cat > "$stage_dir/DEBIAN/control" <<CONTROL
Package: disklens
Version: ${package_version}
Section: utils
Priority: optional
Architecture: ${package_arch}
Installed-Size: ${installed_kib}
Maintainer: broruan <broruan@users.noreply.github.com>
Homepage: https://github.com/broruan/Clean_app
Depends: ${package_depends}, ${svg_dependency}, pkexec, xdg-utils
Recommends: fonts-noto-cjk
Description: Local disk usage and installed software explorer
 Qt desktop application for DEB, Snap and Flatpak inventory, protected removal,
 large-file scanning and user cache/log cleanup. The current UI is Chinese.
 Built natively for Ubuntu ${ubuntu_version} (${VERSION_CODENAME}), ${package_arch}.
CONTROL
(
    cd "$stage_dir"
    find usr -type f -print0 | LC_ALL=C sort -z | xargs -0 md5sum > DEBIAN/md5sums
)
package_path="${output_dir}/disklens_${package_version}_ubuntu${ubuntu_version}_${package_arch}.deb"
dpkg-deb --root-owner-group --build "$stage_dir" "$package_path"
bash "$project_dir/packaging/verify-deb.sh" "$package_path"
printf '\nPackage ready: %s\n' "$package_path"
