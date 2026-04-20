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

# img 폴더가 있으면 PNG→PPM 변환 후 /nfsroot/img/ 으로 복사
if [[ -d "${WORK_DIR}/img" ]]; then
    echo "  img 폴더 PNG→PPM 변환 후 복사: /nfsroot/img/"
    sudo mkdir -p /nfsroot/img
    for f in "${WORK_DIR}/img/"*.png; do
        [ -f "$f" ] || continue
        base=$(basename "${f%.png}")
        outfile="/nfsroot/img/${base}.ppm"
        echo "  변환: $f -> $outfile"
        if command -v convert &>/dev/null; then
            sudo convert "$f" -compress none "$outfile" && echo "  [OK] ImageMagick"
        elif command -v ffmpeg &>/dev/null; then
            sudo ffmpeg -y -i "$f" "$outfile" 2>&1 | tail -1 && echo "  [OK] ffmpeg"
        elif command -v python3 &>/dev/null; then
            sudo python3 - "$f" "$outfile" <<'PYEOF'
import sys
# stdlib only: read PNG via zlib
import struct, zlib
def read_png_to_ppm(src, dst):
    with open(src, 'rb') as f:
        data = f.read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', "not PNG"
    i = 8
    chunks = {}
    raw_idat = b''
    while i < len(data):
        length = struct.unpack('>I', data[i:i+4])[0]
        ctype  = data[i+4:i+8]
        cdata  = data[i+8:i+8+length]
        i += 12 + length
        if ctype == b'IHDR':
            chunks['IHDR'] = cdata
        elif ctype == b'IDAT':
            raw_idat += cdata
        elif ctype == b'IEND':
            break
    w, h, bd, ct = struct.unpack('>IIBB', chunks['IHDR'][:10])
    assert bd == 8 and ct == 2, f"only RGB8 PNG supported (bd={bd} ct={ct})"
    raw = zlib.decompress(raw_idat)
    stride = w * 3 + 1
    pixels = bytearray()
    for row in range(h):
        ftype = raw[row * stride]
        row_data = bytearray(raw[row * stride + 1 : row * stride + 1 + w * 3])
        if ftype == 0:
            pass
        elif ftype == 1:
            for k in range(3, len(row_data)):
                row_data[k] = (row_data[k] + row_data[k-3]) & 0xff
        elif ftype == 2 and row > 0:
            prev = pixels[(row-1)*w*3 : row*w*3]
            for k in range(len(row_data)):
                row_data[k] = (row_data[k] + prev[k]) & 0xff
        pixels += row_data
    with open(dst, 'wb') as f:
        f.write(f'P6\n{w} {h}\n255\n'.encode())
        f.write(bytes(pixels))
    print(f"  [OK] python3 {w}x{h}")
read_png_to_ppm(sys.argv[1], sys.argv[2])
PYEOF
        else
            echo "  WARNING: 변환 도구 없음 (convert/ffmpeg/python3 필요)"
        fi
        # 결과 확인
        if [[ -f "$outfile" ]]; then
            echo "  생성됨: $(ls -lh $outfile | awk '{print $5, $9}')"
        else
            echo "  ERROR: $outfile 생성 실패!"
        fi
    done
fi

echo ""
echo "=== 완료! ==="
echo "보드에서 실행:"
echo "  cd /mnt/nfs"
echo "  ./${TARGET_NAME}"