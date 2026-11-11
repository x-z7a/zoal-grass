#!/usr/bin/env bash
set -euo pipefail

# Provision what building the plugin needs: the X-Plane SDK, at a pinned version, into .cache/sdk.
# ZOAL_GRASS_SDK_ROOT points at a local SDK checkout to reuse instead of downloading.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cache_dir="${repo_root}/.cache"
sdk_root="${cache_dir}/sdk"
sdk_version="${ZOAL_GRASS_SDK_VERSION:-410}"

mkdir -p "${cache_dir}"

if [ -n "${ZOAL_GRASS_SDK_ROOT:-}" ]; then
  if [ ! -f "${ZOAL_GRASS_SDK_ROOT}/CHeaders/XPLM/XPLMPlugin.h" ]; then
    echo "ZOAL_GRASS_SDK_ROOT does not look like an X-Plane SDK: ${ZOAL_GRASS_SDK_ROOT}" >&2
    exit 1
  fi
  rm -rf "${sdk_root}"
  cp -R "${ZOAL_GRASS_SDK_ROOT}" "${sdk_root}"
  echo "Using X-Plane SDK from ${ZOAL_GRASS_SDK_ROOT}"
elif [ ! -f "${sdk_root}/CHeaders/XPLM/XPLMPlugin.h" ]; then
  sdk_zip="${cache_dir}/XPSDK${sdk_version}.zip"
  if [ ! -f "${sdk_zip}" ]; then
    echo "Downloading X-Plane SDK ${sdk_version}..."
    curl -fsSL -o "${sdk_zip}" \
      "https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK${sdk_version}.zip"
  fi
  echo "Extracting X-Plane SDK..."
  rm -rf "${sdk_root}" "${cache_dir}/SDK"
  unzip -q -o "${sdk_zip}" -d "${cache_dir}"
  mv "${cache_dir}/SDK" "${sdk_root}"
fi

echo "X-Plane SDK ready: ${sdk_root}"

# The settings window (src/gui): Dear ImGui (core only: X-Plane 12.4.4's panel graphics draw it), FreeType to
# rasterise the fonts and Inter, the typeface -- the same pinned versions as zoal-atc, checked against
# scripts/*-version.txt. Extracted with cmake, which every build host has.
fetch_pinned() {   # name, pin file, url template (VERSION / VNUM), root template (VERSION), marker inside root
  local name="$1" pin="$2" url="$3" root="$4" marker="$5"
  local version checksum
  read -r version checksum < <(tr -d '\r' < "${repo_root}/scripts/${pin}")
  url="${url//VERSION/${version}}"
  url="${url//VNUM/${version#v}}"
  root="${root//VERSION/${version}}"
  if [ -f "${root}/${marker}" ]; then
    echo "${name} ${version} ready: ${root}"
    return
  fi
  local work actual
  work="$(mktemp -d)"
  echo "Downloading ${name} ${version}..."
  curl -fsSL -o "${work}/archive" "${url}"
  actual="$(shasum -a 256 "${work}/archive" 2>/dev/null || sha256sum "${work}/archive")"
  actual="${actual%% *}"
  if [ "${actual}" != "${checksum}" ]; then
    echo "${name} ${version} archive checksum is ${actual}, want ${checksum}" >&2
    rm -rf "${work}"
    exit 1
  fi
  mkdir -p "${work}/src"
  (cd "${work}/src" && cmake -E tar xf "${work}/archive")
  rm -rf "${root}"
  if [ "${name}" = "Inter" ]; then   # only the three weights the window uses, and the licence
    mkdir -p "${root}"
    for w in Inter-Regular.ttf Inter-SemiBold.ttf Inter-Bold.ttf; do
      cp "${work}/src/extras/ttf/${w}" "${root}/${w}"
    done
    cp "${work}/src/LICENSE.txt" "${root}/LICENSE.txt"
  else
    mv "${work}/src/"*/ "${root}"
  fi
  rm -rf "${work}"
  echo "${name} ${version} ready: ${root}"
}

fetch_pinned "Dear ImGui" imgui-version.txt "https://github.com/ocornut/imgui/archive/refs/tags/VERSION.tar.gz" \
  "${cache_dir}/imgui-VERSION" imgui.h
fetch_pinned "FreeType" freetype-version.txt "https://github.com/freetype/freetype/archive/refs/tags/VERSION.tar.gz" \
  "${cache_dir}/freetype-VERSION" include/ft2build.h
fetch_pinned "Inter" inter-version.txt "https://github.com/rsms/inter/releases/download/VERSION/Inter-VNUM.zip" \
  "${cache_dir}/inter-VERSION" Inter-Bold.ttf
