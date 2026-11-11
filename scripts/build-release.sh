#!/usr/bin/env bash
set -euo pipefail

# Lay out dist/zoal-grass exactly as it goes into <X-Plane 12>/Resources/plugins/:
#   zoal-grass/{mac_x64,win_x64,lin_x64}/zoal-grass.xpl, resources/, licenses/, README.md
# Binaries are taken from build-plugin/<platform>/zoal-grass.xpl (CI puts each runner's build there).
# ZOAL_GRASS_REQUIRE_ALL_PLATFORMS=1 fails when one is missing, so a release never ships a platform short.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${ZOAL_GRASS_BUILD_DIR:-${repo_root}/build-plugin}"
dist="${repo_root}/dist/zoal-grass"

rm -rf "${dist}"
mkdir -p "${dist}"
found=0
for platform in mac_x64 win_x64 lin_x64; do
  xpl="${build_dir}/${platform}/zoal-grass.xpl"
  if [ -f "${xpl}" ]; then
    mkdir -p "${dist}/${platform}"
    cp "${xpl}" "${dist}/${platform}/zoal-grass.xpl"
    found=$((found + 1))
  elif [ "${ZOAL_GRASS_REQUIRE_ALL_PLATFORMS:-0}" = "1" ]; then
    echo "Missing ${xpl}" >&2
    exit 1
  fi
done
if [ "${found}" -eq 0 ]; then
  echo "No zoal-grass.xpl found under ${build_dir}" >&2
  exit 1
fi
cp -R "${repo_root}/resources" "${dist}/resources"
cp -R "${repo_root}/licenses" "${dist}/licenses"
cp "${repo_root}/README.md" "${dist}/README.md"
echo "Packaged ${found} platform(s) into ${dist}"
