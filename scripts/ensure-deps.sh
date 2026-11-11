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
