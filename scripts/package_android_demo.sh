#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/artifacts"
ZIP_PATH="${OUT_DIR}/android_vulkan_sortfree_demo.zip"

mkdir -p "${OUT_DIR}"
rm -f "${ZIP_PATH}"

cd "${ROOT_DIR}"
zip -r "${ZIP_PATH}" android_vulkan_sortfree_demo \
  -x "*/.gradle/*" "*/build/*" "*/.idea/*" "*/local.properties"

echo "Created: ${ZIP_PATH}"
