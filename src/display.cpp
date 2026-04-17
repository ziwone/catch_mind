#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <fstream>

static bool getGlyph5x7(char ch, unsigned char out[7]) {
    memset(out, 0, 7);
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }

    switch (ch) {
        case 'A': { unsigned char g[7] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}; memcpy(out, g, 7); return true; }
        case 'B': { unsigned char g[7] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}; memcpy(out, g, 7); return true; }
        case 'C': { unsigned char g[7] = {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}; memcpy(out, g, 7); return true; }
        case 'D': { unsigned char g[7] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}; memcpy(out, g, 7); return true; }
        case 'E': { unsigned char g[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}; memcpy(out, g, 7); return true; }
        case 'F': { unsigned char g[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}; memcpy(out, g, 7); return true; }
        case 'G': { unsigned char g[7] = {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e}; memcpy(out, g, 7); return true; }
        case 'H': { unsigned char g[7] = {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}; memcpy(out, g, 7); return true; }
        case 'I': { unsigned char g[7] = {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}; memcpy(out, g, 7); return true; }
        case 'J': { unsigned char g[7] = {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e}; memcpy(out, g, 7); return true; }
        case 'K': { unsigned char g[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}; memcpy(out, g, 7); return true; }
        case 'L': { unsigned char g[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}; memcpy(out, g, 7); return true; }
        case 'M': { unsigned char g[7] = {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}; memcpy(out, g, 7); return true; }
        case 'N': { unsigned char g[7] = {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}; memcpy(out, g, 7); return true; }
        case 'O': { unsigned char g[7] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}; memcpy(out, g, 7); return true; }
        case 'P': { unsigned char g[7] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}; memcpy(out, g, 7); return true; }
        case 'Q': { unsigned char g[7] = {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}; memcpy(out, g, 7); return true; }
        case 'R': { unsigned char g[7] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}; memcpy(out, g, 7); return true; }
        case 'S': { unsigned char g[7] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}; memcpy(out, g, 7); return true; }
        case 'T': { unsigned char g[7] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}; memcpy(out, g, 7); return true; }
        case 'U': { unsigned char g[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}; memcpy(out, g, 7); return true; }
        case 'V': { unsigned char g[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}; memcpy(out, g, 7); return true; }
        case 'W': { unsigned char g[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}; memcpy(out, g, 7); return true; }
        case 'X': { unsigned char g[7] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}; memcpy(out, g, 7); return true; }
        case 'Y': { unsigned char g[7] = {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}; memcpy(out, g, 7); return true; }
        case 'Z': { unsigned char g[7] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}; memcpy(out, g, 7); return true; }
        case '0': { unsigned char g[7] = {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}; memcpy(out, g, 7); return true; }
        case '1': { unsigned char g[7] = {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}; memcpy(out, g, 7); return true; }
        case '2': { unsigned char g[7] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}; memcpy(out, g, 7); return true; }
        case '3': { unsigned char g[7] = {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}; memcpy(out, g, 7); return true; }
        case '4': { unsigned char g[7] = {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}; memcpy(out, g, 7); return true; }
        case '5': { unsigned char g[7] = {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}; memcpy(out, g, 7); return true; }
        case '6': { unsigned char g[7] = {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e}; memcpy(out, g, 7); return true; }
        case '7': { unsigned char g[7] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}; memcpy(out, g, 7); return true; }
        case '8': { unsigned char g[7] = {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}; memcpy(out, g, 7); return true; }
        case '9': { unsigned char g[7] = {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e}; memcpy(out, g, 7); return true; }
        case '.': { unsigned char g[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}; memcpy(out, g, 7); return true; }
        case '-': { unsigned char g[7] = {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}; memcpy(out, g, 7); return true; }
        case ':': { unsigned char g[7] = {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}; memcpy(out, g, 7); return true; }
        case '/': { unsigned char g[7] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}; memcpy(out, g, 7); return true; }
        case ' ': { return true; }
        default: { return false; }
    }
}

// 색상 정의
const unsigned int Display::COLOR_RED = 0xff0000;
const unsigned int Display::COLOR_GREEN = 0x00ff00;
const unsigned int Display::COLOR_BLUE = 0x0000ff;
const unsigned int Display::COLOR_BLACK = 0x000000;
const unsigned int Display::COLOR_WHITE = 0xffffff;
const unsigned int Display::COLOR_YELLOW = 0xffff00;

Display::Display()
        : fd(-1),
            map(nullptr),
            size(0),
            bytesPerPixel(0),
            pageFlipEnabled(false),
            inFrame(false),
            frontPage(0),
            backPage(1),
            pageSizeBytes(0),
            isInitialized(false) {
}

Display::~Display() {
    close_fb();
}

bool Display::init() {
    const char *FBDEV = "/dev/fb0";
    
    // 1. framebuffer 장치 열기
    fd = open(FBDEV, O_RDWR);
    if (fd == -1) {
        perror("open /dev/fb0 failed");
        return false;
    }
    printf("[Display] %s opened\n", FBDEV);
    
    // 2. 가변 스크린 정보 가져오기
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("ioctl FBIOGET_VSCREENINFO failed");
        ::close(fd);
        return false;
    }
    printf("[Display] Resolution: %dx%d, %d bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    
    // 3. 고정 스크린 정보 가져오기
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("ioctl FBIOGET_FSCREENINFO failed");
        ::close(fd);
        return false;
    }
    printf("[Display] line_length: %d bytes\n", finfo.line_length);
    
    bytesPerPixel = vinfo.bits_per_pixel / 8;
    if (bytesPerPixel <= 0) {
        std::fprintf(stderr, "[Display] Unsupported bpp=%d\n", vinfo.bits_per_pixel);
        ::close(fd);
        return false;
    }

    // 4. 가능한 경우 페이지 플립(더블버퍼) 시도
    fb_var_screeninfo requested = vinfo;
    requested.yoffset = 0;
    requested.xoffset = 0;
    requested.activate = FB_ACTIVATE_NOW;
    if (requested.yres_virtual < requested.yres * 2) {
        requested.yres_virtual = requested.yres * 2;
    }

    if (ioctl(fd, FBIOPUT_VSCREENINFO, &requested) == 0) {
        if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == 0) {
            int stride = finfo.line_length;
            int pageBytes = stride * (int)vinfo.yres;
            if (vinfo.yres_virtual >= vinfo.yres * 2 && (int)finfo.smem_len >= pageBytes * 2) {
                pageFlipEnabled = true;
                pageSizeBytes = pageBytes;
                std::printf("[Display] Page flip enabled (virtual y=%d)\n", vinfo.yres_virtual);
            }
        }
    }

    if (!pageFlipEnabled) {
        pageSizeBytes = finfo.line_length * (int)vinfo.yres;
        std::printf("[Display] Page flip unavailable; single buffer mode\n");
    }

    // 5. 메모리 매핑
    size = (int)finfo.smem_len;
    map = (char *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == (char *)-1) {
        perror("mmap failed");
        ::close(fd);
        return false;
    }
    printf("[Display] Memory mapped at %p, size: %d bytes\n", map, size);

    if (pageFlipEnabled) {
        fb_var_screeninfo pan = vinfo;
        pan.yoffset = 0;
        pan.activate = FB_ACTIVATE_NOW;
        ioctl(fd, FBIOPAN_DISPLAY, &pan);
        frontPage = 0;
        backPage = 1;
    }
    
    isInitialized = true;
    return true;
}

void Display::close_fb() {
    if (isInitialized) {
        if (map != nullptr) {
            munmap(map, size);
            map = nullptr;
        }
        if (fd != -1) {
            ::close(fd);
            fd = -1;
        }
        isInitialized = false;
    }
}

void Display::clearScreen(unsigned int color) {
    drawRect(0, 0, vinfo.xres, vinfo.yres, color);
}

static inline void writePixelToPage(char *map,
                                    const fb_var_screeninfo &vinfo,
                                    const fb_fix_screeninfo &finfo,
                                    int bytesPerPixel,
                                    int pageIndex,
                                    int x,
                                    int y,
                                    unsigned int color) {
    int baseY = y + (pageIndex * (int)vinfo.yres);
    int location = (x + (int)vinfo.xoffset) * bytesPerPixel +
                   baseY * (int)finfo.line_length;

    if (bytesPerPixel == 4) {
        *(unsigned int *)(map + location) = color;
    } else if (bytesPerPixel == 2) {
        // RGB565
        unsigned int r = (color >> 16) & 0xff;
        unsigned int g = (color >> 8) & 0xff;
        unsigned int b = color & 0xff;
        unsigned short c565 = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        *(unsigned short *)(map + location) = c565;
    }
}

void Display::drawPixel(int x, int y, unsigned int color) {
    if (x < 0 || x >= (int)vinfo.xres || y < 0 || y >= (int)vinfo.yres) {
        return;
    }

    if (!pageFlipEnabled) {
        writePixelToPage(map, vinfo, finfo, bytesPerPixel, 0, x, y, color);
        return;
    }

    if (inFrame) {
        writePixelToPage(map, vinfo, finfo, bytesPerPixel, backPage, x, y, color);
    } else {
        // 프레임 바깥 즉시 드로우는 두 페이지에 동일 반영
        writePixelToPage(map, vinfo, finfo, bytesPerPixel, frontPage, x, y, color);
        writePixelToPage(map, vinfo, finfo, bytesPerPixel, backPage, x, y, color);
    }
}

void Display::beginFrame() {
    if (!pageFlipEnabled || inFrame || map == nullptr) {
        return;
    }

    std::memcpy(map + backPage * pageSizeBytes,
                map + frontPage * pageSizeBytes,
                pageSizeBytes);
    inFrame = true;
}

void Display::endFrame() {
    if (!pageFlipEnabled || !inFrame) {
        return;
    }

    fb_var_screeninfo pan = vinfo;
    pan.yoffset = backPage * (int)vinfo.yres;
    pan.activate = FB_ACTIVATE_NOW;
    if (ioctl(fd, FBIOPAN_DISPLAY, &pan) == 0) {
        int oldFront = frontPage;
        frontPage = backPage;
        backPage = oldFront;
    }
    inFrame = false;
}

void Display::drawRect(int x, int y, int w, int h, unsigned int color) {
    int xx, yy;
    
    for (yy = y; yy < (y + h); yy++) {
        for (xx = x; xx < (x + w); xx++) {
            drawPixel(xx, yy, color);
        }
    }
}

void Display::drawText(int x, int y, const std::string &text, unsigned int color, int scale) {
    if (scale < 1) {
        scale = 1;
    }

    int cursorX = x;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char glyph[7];
        if (!getGlyph5x7(text[i], glyph)) {
            cursorX += (6 * scale);
            continue;
        }

        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph[row] >> (4 - col)) & 0x01) {
                    drawRect(cursorX + (col * scale), y + (row * scale), scale, scale, color);
                }
            }
        }

        cursorX += (6 * scale);
    }
}

bool Display::saveFrameToPPM(const std::string &path) const {
    if (!isInitialized || map == nullptr) {
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    int width = (int)vinfo.xres;
    int height = (int)vinfo.yres;
    out << "P6\n" << width << " " << height << "\n255\n";

    int page = pageFlipEnabled ? frontPage : 0;
    int stride = (int)finfo.line_length;

    for (int y = 0; y < height; ++y) {
        const unsigned char *row = (const unsigned char *)map + (page * height + y) * stride;
        for (int x = 0; x < width; ++x) {
            unsigned char rgb[3] = {0, 0, 0};
            if (bytesPerPixel == 4) {
                unsigned int c = *(const unsigned int *)(row + x * 4);
                rgb[0] = (unsigned char)((c >> 16) & 0xff);
                rgb[1] = (unsigned char)((c >> 8) & 0xff);
                rgb[2] = (unsigned char)(c & 0xff);
            } else if (bytesPerPixel == 2) {
                unsigned short c565 = *(const unsigned short *)(row + x * 2);
                rgb[0] = (unsigned char)(((c565 >> 11) & 0x1f) * 255 / 31);
                rgb[1] = (unsigned char)(((c565 >> 5) & 0x3f) * 255 / 63);
                rgb[2] = (unsigned char)((c565 & 0x1f) * 255 / 31);
            }
            out.write((const char *)rgb, 3);
        }
    }

    return true;
}
