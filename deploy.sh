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

# 2. rsync로 동기화 (공유폴더 전체 복사)
#    bgm, build 등 모든 하위 폴더/파일을 포함한다.
sudo rsync -av --delete \
    --exclude='.git/' \
    --exclude='.vscode/' \
    ${SHARED_DIR}/ \
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

# bgm 폴더가 있으면 /nfsroot/bgm/ 으로 복사
if [[ -d "${WORK_DIR}/bgm" ]]; then
    echo "  bgm 폴더 복사: /nfsroot/bgm/"
    sudo rsync -av --delete "${WORK_DIR}/bgm/" /nfsroot/bgm/
fi

# img 폴더가 있으면 PNG→PPM 변환 후 /nfsroot/img/ 으로 복사 (서브디렉토리 포함)
if [[ -d "${WORK_DIR}/img" ]]; then
    echo "  img 폴더 PNG→PPM 변환 후 복사: /nfsroot/img/"
    sudo mkdir -p /nfsroot/img
    while IFS= read -r -d '' f; do
        [ -f "$f" ] || continue
        relpath="${f#${WORK_DIR}/img/}"
        outfile="/nfsroot/img/${relpath%.png}.ppm"
        sudo mkdir -p "$(dirname "$outfile")"
        echo "  변환: ${relpath} -> ${relpath%.png}.ppm"
        sudo python3 "${WORK_DIR}/scripts/png2ppm.py" "$f" "$outfile"
        if [[ -f "$outfile" ]]; then
            echo "  생성됨: $(ls -lh $outfile | awk '{print $5, $9}')"
        else
            echo "  ERROR: $outfile 생성 실패!"
        fi
    done < <(find "${WORK_DIR}/img" -name "*.png" -print0)
fi

echo ""
echo "=== 완료! ==="
echo "보드에서 실행:"
echo "  cd /mnt/nfs"
echo "  ./${TARGET_NAME}"