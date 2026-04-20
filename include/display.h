#ifndef DISPLAY_H
#define DISPLAY_H

#ifndef USE_QT_DISPLAY
#include <linux/fb.h>
#endif
#include <string>

// Framebuffer를 사용한 디스플레이 제어
class Display {
private:
#ifndef USE_QT_DISPLAY
    int fd;
    char *map;
    int size;
    int bytesPerPixel;

    bool pageFlipEnabled;
    bool softwareDoubleBufferEnabled;
    bool inFrame;
    int frontPage;
    int backPage;
    int pageSizeBytes;
    char *shadowBuffer;
    
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
#else
    // Qt backend에서는 실제 타입을 cpp에서만 다루기 위해 void*로 보관
    void *qtApp;
    void *qtWindow;
    void *qtImage;
    int width;
    int height;
    int bitsPerPixel;
    bool pageFlipEnabled;
    bool softwareDoubleBufferEnabled;
    bool inFrame;
#endif
    
    bool isInitialized;
    
public:
    Display();
    ~Display();
    
    // 초기화 및 정리
    bool init();
    void close_fb();
    
    // 화면 정보
        int getWidth() const {
    #ifndef USE_QT_DISPLAY
        return vinfo.xres;
    #else
        return width;
    #endif
        }
        int getHeight() const {
    #ifndef USE_QT_DISPLAY
        return vinfo.yres;
    #else
        return height;
    #endif
        }
        int getBitsPerPixel() const {
    #ifndef USE_QT_DISPLAY
        return vinfo.bits_per_pixel;
    #else
        return bitsPerPixel;
    #endif
        }
    
    // 그리기 함수
    void drawRect(int x, int y, int w, int h, unsigned int color);
    void clearScreen(unsigned int color = 0x000000);
    void drawPixel(int x, int y, unsigned int color);
    void drawText(int x, int y, const std::string &text, unsigned int color, int scale = 2);
    void beginFrame();
    void endFrame();
    bool isPageFlipEnabled() const { return pageFlipEnabled; }
    bool isSoftwareDoubleBufferEnabled() const { return softwareDoubleBufferEnabled; }
    bool saveFrameToPPM(const std::string &path) const;
    // PNG 이미지 불러오기 (libpng 필요)
    // dstX/Y/W/H: 화면상 그리기 영역, 자동 스케일
    bool drawPNG(const std::string &path, int dstX, int dstY, int dstW, int dstH, bool skipWhite = false);
    
    // 편의 색상
    static const unsigned int COLOR_RED;
    static const unsigned int COLOR_GREEN;
    static const unsigned int COLOR_BLUE;
    static const unsigned int COLOR_BLACK;
    static const unsigned int COLOR_WHITE;
    static const unsigned int COLOR_YELLOW;
};

#endif
