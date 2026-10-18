#!/usr/bin/env bash
# Inspect and run --version only; never installs, uninstalls or cleans user data.
set -euo pipefail
export LC_ALL=C

[[ $# == 1 ]] || { echo 'Usage: bash packaging/verify-deb.sh PACKAGE.deb' >&2; exit 2; }
package_path="$(realpath -- "$1")"
work_dir="$(mktemp -d)"
trap 'rm -rf -- "$work_dir"' EXIT
dpkg-deb --raw-extract "$package_path" "$work_dir"
package_name="$(dpkg-deb --field "$package_path" Package)"
package_version="$(dpkg-deb --field "$package_path" Version)"
package_arch="$(dpkg-deb --field "$package_path" Architecture)"
[[ "$package_name" == disklens ]] || { echo 'Unexpected package name.' >&2; exit 1; }
[[ "$package_arch" == "$(dpkg --print-architecture)" ]] || {
    echo 'Verify the package on its native architecture.' >&2; exit 1;
}
machine="$(readelf -h "$work_dir/usr/bin/disklens" | sed -n 's/^ *Machine: *//p')"
case "${package_arch}:${machine}" in
    amd64:'Advanced Micro Devices X86-64'|arm64:AArch64) ;;
    *) echo "ELF architecture does not match DEB metadata: $package_arch / $machine" >&2; exit 1 ;;
esac
[[ -s "$work_dir/usr/share/icons/hicolor/scalable/apps/disklens.svg" ]]
[[ -s "$work_dir/usr/share/doc/disklens/copyright" ]]
gzip -t "$work_dir/usr/share/doc/disklens/changelog.gz"
desktop-file-validate "$work_dir/usr/share/applications/disklens.desktop"
xmllint --noout "$work_dir/usr/share/icons/hicolor/scalable/apps/disklens.svg"
xmllint --noout "$work_dir/usr/share/metainfo/io.github.broruan.DiskLens.metainfo.xml"
(
    cd "$work_dir"
    md5sum --quiet --check DEBIAN/md5sums
)
dependencies="$(dpkg-deb --field "$package_path" Depends)"
[[ "$dependencies" =~ (^|,\ )pkexec(,|\ |$) ]] || { echo 'Missing pkexec dependency.' >&2; exit 1; }
if ldd "$work_dir/usr/bin/disklens" | grep -q 'not found'; then
    echo 'Unresolved runtime library dependency.' >&2; exit 1
fi
actual_version="$(QT_QPA_PLATFORM=offscreen "$work_dir/usr/bin/disklens" --version)"
[[ "$actual_version" == "DiskLens $package_version" ]] || {
    echo "Version mismatch: expected DiskLens $package_version, got $actual_version" >&2; exit 1;
}
printf 'Verified %s %s (%s): metadata, ELF, dependencies, icon, desktop file, checksums, --version.\n' \
    "$package_name" "$package_version" "$package_arch"
