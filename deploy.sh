#!/bin/bash

# 공유폴더 → 작업폴더 동기화 후 빌드
# VirtualBox에서 실행: bash /media/sf_share/catch_mind/deploy.sh

SHARED_DIR="/media/sf_share/catch_mind"
WORK_DIR="/home/user/work/proj"

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
make catch_mind
sudo cp catch_mind /nfsroot/

echo ""
echo "=== 완료! ==="
echo "보드에서 실행:"
echo "  cd /mnt/nfs"
echo "  ./catch_mind"
