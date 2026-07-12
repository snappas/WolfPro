#!/bin/bash
set -euo pipefail

# Non-interactive CI counterpart to build-game64.sh + build-server64.sh.
# The interactive originals are left untouched for local manual use; this
# script drops the two `read -p "Press return"` pauses (the Windows DLLs are
# supplied via WIN_X64_DIR/WIN_X86_DIR instead of a manual Explorer copy) and
# adds the `docker push` step that was previously run by hand.
#
# Required env vars:
#   DOCKER_USER  - Docker Hub username/org, e.g. rtcwpro
#   DOCKER_TAG   - image tag to build/push, e.g. dev or master
#   WIN_X64_DIR  - path to the extracted windows-x64 build artifact
#   WIN_X86_DIR  - path to the extracted windows-x86 build artifact
# Optional env vars:
#   SKIP_PUSH    - if "1", build the server image but don't push it

: "${DOCKER_USER:?DOCKER_USER is required}"
: "${DOCKER_TAG:?DOCKER_TAG is required}"
: "${WIN_X64_DIR:?WIN_X64_DIR is required}"
: "${WIN_X86_DIR:?WIN_X86_DIR is required}"

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RTCW_SRC=$(dirname "$SCRIPT_DIR")

echo "== Building rtcw-compile64 image and running make all =="
docker build \
  --build-arg BRANCH="${DOCKER_TAG}" \
  -t "${DOCKER_USER}/rtcw:${DOCKER_TAG}" \
  -f "${SCRIPT_DIR}/dockerfiles/rtcw-compile64" "${SCRIPT_DIR}/dockerfiles"

docker run \
  --user "$(id -u):$(id -g)" \
  -v "${RTCW_SRC}:/workspace" \
  --workdir /workspace/src \
  "${DOCKER_USER}/rtcw:${DOCKER_TAG}" \
  make all

echo "== Merging Windows mod DLLs into build64/wolfpro =="
cp "${WIN_X64_DIR}/wolfpro/"*.dll "${RTCW_SRC}/build64/wolfpro/"
cp "${WIN_X86_DIR}/wolfpro/"*.dll "${RTCW_SRC}/build64/wolfpro/"

echo "== Packaging combined pk3s =="
# wolfpro_bin.pk3/wolfpro_server.pk3 already exist at this point (CMake's own
# mod_pk3 target and make all's internal build-pk3 both created Linux-only
# versions earlier). `zip -r` updates an existing archive rather than
# recreating it, which risks a stale/corrupted archive across repeated
# passes — start clean so this pass produces one correct combined pk3.
rm -f "${RTCW_SRC}/build64/wolfpro/wolfpro_bin.pk3" "${RTCW_SRC}/build64/wolfpro/wolfpro_server.pk3"
(cd "${RTCW_SRC}/src" && make -f makefile build-pk3)

PK3_DATE=$(date +%Y%m%d)
mv "${RTCW_SRC}/build64/wolfpro/wolfpro_bin.pk3" "${RTCW_SRC}/build64/wolfpro/wolfpro_bin-${PK3_DATE}.pk3"
mv "${RTCW_SRC}/build64/wolfpro/wolfpro_server.pk3" "${RTCW_SRC}/build64/wolfpro/wolfpro_server-${PK3_DATE}.pk3"
mv "${RTCW_SRC}/build64/wolfpro/wolfpro_assets.pk3" "${RTCW_SRC}/build64/wolfpro/wolfpro_assets-${PK3_DATE}.pk3"
cp "${RTCW_SRC}/wolfpro/"*.cfg "${RTCW_SRC}/build64/wolfpro/"
rm -f "${RTCW_SRC}/build64/wolfpro/wolfpro_assets.bin"

echo "== Fetching Omnibot RTCW release =="
OMNIBOT_ZIP_URL=$(curl -fsSL https://api.github.com/repos/jswigart/omni-bot/releases/latest \
  | grep -o '"browser_download_url": *"[^"]*_RTCW\.zip"' \
  | head -n1 \
  | cut -d '"' -f 4)
if [ -z "$OMNIBOT_ZIP_URL" ]; then
  echo "Error: could not find an *_RTCW.zip asset in the latest jswigart/omni-bot release" >&2
  exit 1
fi
curl -fsSL "$OMNIBOT_ZIP_URL" -o /tmp/omni-bot-release.zip
rm -rf /tmp/omni-bot-release
unzip -q /tmp/omni-bot-release.zip -d /tmp/omni-bot-release
rm -rf "${RTCW_SRC}/build64/wolfpro/omni-bot"
cp -r /tmp/omni-bot-release/omni-bot "${RTCW_SRC}/build64/wolfpro/omni-bot"
rm -rf /tmp/omni-bot-release /tmp/omni-bot-release.zip

echo "== Building rtcw-server64 image =="
if [ -d "${SCRIPT_DIR}/dockerfiles/build64" ]; then
  rm -rf "${SCRIPT_DIR}/dockerfiles/build64"
fi
cp -r "${RTCW_SRC}/build64" "${SCRIPT_DIR}/dockerfiles"
# wolfpro_server.pk3 is a release asset, not something the dedicated server
# image itself should ship — remove it from the copied build context only
# (the original under build64/wolfpro is left alone for the release upload).
rm -f "${SCRIPT_DIR}/dockerfiles/build64/wolfpro/wolfpro_server-${PK3_DATE}.pk3"

docker build \
  --build-arg IMAGE="${DOCKER_USER}/rtcw:${DOCKER_TAG}" \
  -t "${DOCKER_USER}/rtcw-server64:${DOCKER_TAG}" \
  -f "${SCRIPT_DIR}/dockerfiles/rtcw-server64" "${SCRIPT_DIR}/dockerfiles"

if [ "${SKIP_PUSH:-0}" != "1" ]; then
  echo "== Pushing ${DOCKER_USER}/rtcw-server64:${DOCKER_TAG} =="
  docker push "${DOCKER_USER}/rtcw-server64:${DOCKER_TAG}"
else
  echo "SKIP_PUSH=1 set, not pushing"
fi

echo "PK3_DATE=${PK3_DATE}"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "pk3_date=${PK3_DATE}" >> "$GITHUB_OUTPUT"
fi
