#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>

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

Display::Display() : fd(-1), map(nullptr), size(0), isInitialized(false) {
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
    
    // 4. 메모리 매핑
    size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    map = (char *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == (char *)-1) {
        perror("mmap failed");
        ::close(fd);
        return false;
    }
    printf("[Display] Memory mapped at %p, size: %d bytes\n", map, size);
    
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

void Display::drawPixel(int x, int y, unsigned int color) {
    if (x < 0 || x >= (int)vinfo.xres || y < 0 || y >= (int)vinfo.yres) {
        return;
    }
    
    int location = (x + vinfo.xoffset) * (vinfo.bits_per_pixel / 8) +
                   (y + vinfo.yoffset) * finfo.line_length;
    
    if (vinfo.bits_per_pixel == 32) {
        *(unsigned int *)(map + location) = color;
    } else if (vinfo.bits_per_pixel == 16) {
        *(unsigned short *)(map + location) = (unsigned short)color;
    }
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
