#!/usr/bin/env python3
"""PNG → PPM P6 변환 (외부 패키지 불필요, stdlib zlib만 사용)"""
import sys, struct, zlib

def png2ppm(src, dst):
    with open(src, 'rb') as f:
        data = f.read()

    assert data[:8] == b'\x89PNG\r\n\x1a\n', "Not a PNG file"

    # 청크 파싱
    i = 8
    ihdr = None
    raw_idat = b''
    while i < len(data):
        length = struct.unpack('>I', data[i:i+4])[0]
        ctype  = data[i+4:i+8]
        cdata  = data[i+8:i+8+length]
        i += 12 + length
        if ctype == b'IHDR':
            ihdr = cdata
        elif ctype == b'IDAT':
            raw_idat += cdata
        elif ctype == b'IEND':
            break

    w, h, bd, ct = struct.unpack('>IIBB', ihdr[:10])
    print(f"  PNG: {w}x{h}, bit_depth={bd}, color_type={ct}")

    # RGBA → RGB 변환 여부
    has_alpha = ct in (4, 6)  # ct=4: gray+alpha, ct=6: RGBA
    is_gray   = ct in (0, 4)  # grayscale

    raw = zlib.decompress(raw_idat)

    channels = 1 if is_gray else 3
    if has_alpha:
        channels += 1
    stride = w * channels + 1  # +1 for filter byte

    pixels = bytearray()
    prev_row = bytearray(w * channels)

    for row_idx in range(h):
        offset = row_idx * stride
        ftype    = raw[offset]
        row_data = bytearray(raw[offset+1 : offset+1 + w*channels])

        # PNG filter 복원
        if ftype == 0:   # None
            pass
        elif ftype == 1: # Sub
            for k in range(channels, len(row_data)):
                row_data[k] = (row_data[k] + row_data[k - channels]) & 0xff
        elif ftype == 2: # Up
            for k in range(len(row_data)):
                row_data[k] = (row_data[k] + prev_row[k]) & 0xff
        elif ftype == 3: # Average
            for k in range(len(row_data)):
                a = row_data[k - channels] if k >= channels else 0
                b = prev_row[k]
                row_data[k] = (row_data[k] + (a + b) // 2) & 0xff
        elif ftype == 4: # Paeth
            for k in range(len(row_data)):
                a = row_data[k - channels] if k >= channels else 0
                b = prev_row[k]
                c = prev_row[k - channels] if k >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if pa<=pb and pa<=pc else (b if pb<=pc else c)
                row_data[k] = (row_data[k] + pr) & 0xff

        prev_row = bytearray(row_data)

        # RGB 추출 (alpha 제거, gray→RGB 확장)
        for px in range(w):
            if is_gray:
                g = row_data[px * channels]
                pixels += bytes([g, g, g])
            else:
                r = row_data[px * channels]
                g = row_data[px * channels + 1]
                b = row_data[px * channels + 2]
                pixels += bytes([r, g, b])

    with open(dst, 'wb') as f:
        f.write(f'P6\n{w} {h}\n255\n'.encode())
        f.write(bytes(pixels))

    print(f"  PPM 저장: {dst} ({len(pixels)//1024} KB)")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.png output.ppm")
        sys.exit(1)
    png2ppm(sys.argv[1], sys.argv[2])
    print("  완료!")
