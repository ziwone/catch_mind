#ifndef DISPLAY_H
#define DISPLAY_H

#include <linux/fb.h>
#include <string>

// Framebuffer를 사용한 디스플레이 제어
class Display {
private:
    int fd;
    char *map;
    int size;
    
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    
    bool isInitialized;
    
public:
    Display();
    ~Display();
    
    // 초기화 및 정리
    bool init();
    void close_fb();
    
    // 화면 정보
    int getWidth() const { return vinfo.xres; }
    int getHeight() const { return vinfo.yres; }
    int getBitsPerPixel() const { return vinfo.bits_per_pixel; }
    
    // 그리기 함수
    void drawRect(int x, int y, int w, int h, unsigned int color);
    void clearScreen(unsigned int color = 0x000000);
    void drawPixel(int x, int y, unsigned int color);
    void drawText(int x, int y, const std::string &text, unsigned int color, int scale = 2);
    bool saveFrameToPPM(const std::string &path) const;
    
    // 편의 색상
    static const unsigned int COLOR_RED;
    static const unsigned int COLOR_GREEN;
    static const unsigned int COLOR_BLUE;
    static const unsigned int COLOR_BLACK;
    static const unsigned int COLOR_WHITE;
    static const unsigned int COLOR_YELLOW;
};

#endif
