#!/bin/bash
set -euo pipefail

# 공유폴더 → 작업폴더 동기화 후 빌드
# VirtualBox에서 실행: bash /media/sf_share/catch_mind/deploy.sh

SHARED_DIR="/media/sf_share/catch_mind"
WORK_DIR="/home/user/work/proj"
TARGET_NAME="${1:-catch_mind}"

echo "=== 1단계: 공유폴더 → 작업폴더 동기화 ==="
echo "  출처: ${SHARED_DIR}"
echo "  대상: ${WORK_DIR}"

# 1. 작업 디렉토리 생성
sudo mkdir -p "${WORK_DIR}"

# 2. rsync로 동기화 (공유폴더의 모든 파일 복사)
sudo rsync -av --delete \
    ${SHARED_DIR}/src \
    ${SHARED_DIR}/include \
    ${SHARED_DIR}/Makefile \
    ${SHARED_DIR}/CMakeLists.txt \
    ${SHARED_DIR}/*.md \
    ${WORK_DIR}/

echo ""
echo "=== 2단계: 빌드 시작 ==="
cd "${WORK_DIR}"
echo "  빌드 타깃: ${TARGET_NAME}"
make "${TARGET_NAME}"

if [[ ! -f "${TARGET_NAME}" ]]; then
    echo "오류: 빌드 산출물 '${TARGET_NAME}' 를 찾을 수 없습니다."
    exit 1
fi

sudo cp "${TARGET_NAME}" /nfsroot/

echo ""
echo "=== 완료! ==="
echo "보드에서 실행:"
echo "  cd /mnt/nfs"
echo "  ./${TARGET_NAME}"