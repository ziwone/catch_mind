/*
 * fb_server.c - /dev/fb0 HTTP 모니터 서버
 *
 * 순수 C (libc만 사용, 외부 라이브러리 없음)
 * 빌드: aarch64-linux-gnu-gcc -O2 -o fb_server src/fb_server.c -lpthread
 * 실행: ./fb_server [포트(기본 8080)]
 * 접속: http://<보드IP>:8080/
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* sysfs에서 FB 해상도/bpp 읽기                                         */
/* ------------------------------------------------------------------ */
static int read_sysfs_int(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    fscanf(f, "%d", &v);
    fclose(f);
    return v;
}

static int fb_get_info(int *w, int *h, int *bpp) {
    /* virtual_size: "WIDTHxHEIGHT" 또는 "WIDTH,HEIGHT" */
    FILE *f = fopen("/sys/class/graphics/fb0/virtual_size", "r");
    if (!f) return -1;
    if (fscanf(f, "%d,%d", w, h) != 2) {
        rewind(f);
        if (fscanf(f, "%dx%d", w, h) != 2) { fclose(f); return -1; }
    }
    fclose(f);

    *bpp = read_sysfs_int("/sys/class/graphics/fb0/bits_per_pixel");
    if (*bpp < 0) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* BMP 생성                                                             */
/* ------------------------------------------------------------------ */
#pragma pack(push, 1)
typedef struct {
    uint16_t type;       /* "BM" */
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} BmpFileHeader;

typedef struct {
    uint32_t header_size;
    int32_t  width;
    int32_t  height;   /* 음수 = top-down */
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_pixels_per_m;
    int32_t  y_pixels_per_m;
    uint32_t colors_used;
    uint32_t colors_important;
} BmpDibHeader;
#pragma pack(pop)

/*
 * 리턴값: 동적 할당된 BMP 버퍼 (해제는 호출자)
 * *out_size 에 크기 저장
 */
static uint8_t *fb_to_bmp(int w, int h, int bpp, size_t *out_size) {
    int fd = open("/dev/fb0", O_RDONLY);
    if (fd < 0) return NULL;

    size_t fb_bytes = (size_t)w * h * (bpp / 8);
    uint8_t *fb = malloc(fb_bytes);
    if (!fb) { close(fd); return NULL; }

    size_t rd = 0;
    while (rd < fb_bytes) {
        ssize_t n = read(fd, fb + rd, fb_bytes - rd);
        if (n <= 0) break;
        rd += n;
    }
    close(fd);

    /* BMP 행 패딩: 4바이트 정렬 */
    int row_stride = w * 3;
    int pad = (4 - (row_stride % 4)) % 4;
    int padded_row = row_stride + pad;
    size_t pixel_size = (size_t)padded_row * h;
    size_t total = sizeof(BmpFileHeader) + sizeof(BmpDibHeader) + pixel_size;

    uint8_t *buf = malloc(total);
    if (!buf) { free(fb); return NULL; }

    /* 헤더 작성 */
    BmpFileHeader *fh = (BmpFileHeader *)buf;
    fh->type      = 0x4D42; /* 'BM' */
    fh->file_size = (uint32_t)total;
    fh->reserved1 = 0;
    fh->reserved2 = 0;
    fh->offset    = sizeof(BmpFileHeader) + sizeof(BmpDibHeader);

    BmpDibHeader *dh = (BmpDibHeader *)(buf + sizeof(BmpFileHeader));
    dh->header_size      = sizeof(BmpDibHeader);
    dh->width            = w;
    dh->height           = -h;  /* top-down */
    dh->planes           = 1;
    dh->bpp              = 24;
    dh->compression      = 0;
    dh->image_size       = (uint32_t)pixel_size;
    dh->x_pixels_per_m   = 0;
    dh->y_pixels_per_m   = 0;
    dh->colors_used      = 0;
    dh->colors_important = 0;

    /* 픽셀 변환 (RGB → BGR for BMP) */
    uint8_t *dst = buf + fh->offset;
    for (int y = 0; y < h; y++) {
        uint8_t *row_dst = dst + (size_t)y * padded_row;
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            if (bpp == 16) {
                uint16_t p;
                memcpy(&p, fb + ((size_t)y * w + x) * 2, 2);
                r = ((p >> 11) & 0x1F) * 255 / 31;
                g = ((p >>  5) & 0x3F) * 255 / 63;
                b = ( p        & 0x1F) * 255 / 31;
            } else if (bpp == 32) {
                /* BGRA (RPi 기본 포맷) */
                const uint8_t *px = fb + ((size_t)y * w + x) * 4;
                b = px[0]; g = px[1]; r = px[2];
            } else { /* 24bpp RGB */
                const uint8_t *px = fb + ((size_t)y * w + x) * 3;
                r = px[0]; g = px[1]; b = px[2];
            }
            row_dst[x * 3 + 0] = b;
            row_dst[x * 3 + 1] = g;
            row_dst[x * 3 + 2] = r;
        }
        /* 패딩 */
        memset(row_dst + row_stride, 0, pad);
    }

    free(fb);
    *out_size = total;
    return buf;
}

/* ------------------------------------------------------------------ */
/* HTTP 핸들러                                                          */
/* ------------------------------------------------------------------ */
static const char HTML[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>Board FB Monitor</title>"
    "<style>body{margin:0;background:#111;color:#aaa;"
    "display:flex;flex-direction:column;align-items:center;"
    "justify-content:center;min-height:100vh;font-family:monospace}"
    "img{max-width:100%;max-height:90vh;border:1px solid #444}"
    "#info{margin:6px;font-size:13px}</style></head><body>"
    "<img id='fb' src='/frame'>"
    "<div id='info'>connecting...</div>"
    "<script>"
    "var img=document.getElementById('fb');"
    "var info=document.getElementById('info');"
    "function refresh(){"
    "var t0=performance.now();"
    "var tmp=new Image();"
    "tmp.onload=function(){"
    "img.src=tmp.src;"
    "var ms=Math.round(performance.now()-t0);"
    "info.textContent=new Date().toLocaleTimeString()+'  |  '+ms+' ms';"
    "setTimeout(refresh,300)};"
    "tmp.onerror=function(){"
    "info.textContent='error - retrying...';"
    "setTimeout(refresh,2000)};"
    "tmp.src='/frame?'+Date.now()}"
    "setTimeout(refresh,300);"
    "</script></body></html>\r\n";

static void handle_client(int csock) {
    char req[512];
    ssize_t n = recv(csock, req, sizeof(req) - 1, 0);
    if (n <= 0) { close(csock); return; }
    req[n] = '\0';

    int is_frame = (strncmp(req, "GET /frame", 10) == 0);
    int is_root  = (strncmp(req, "GET / ", 6) == 0 ||
                    strncmp(req, "GET /\r", 6) == 0);

    if (is_root) {
        send(csock, HTML, sizeof(HTML) - 1, 0);
    } else if (is_frame) {
        int w, h, bpp;
        if (fb_get_info(&w, &h, &bpp) < 0) {
            const char *e = "HTTP/1.1 500 Internal Server Error\r\n"
                            "Connection: close\r\n\r\nFB error";
            send(csock, e, strlen(e), 0);
        } else {
            size_t bmp_size = 0;
            uint8_t *bmp = fb_to_bmp(w, h, bpp, &bmp_size);
            if (!bmp) {
                const char *e = "HTTP/1.1 500 Internal Server Error\r\n"
                                "Connection: close\r\n\r\nalloc error";
                send(csock, e, strlen(e), 0);
            } else {
                char hdr[256];
                int hlen = snprintf(hdr, sizeof(hdr),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: image/bmp\r\n"
                    "Content-Length: %zu\r\n"
                    "Cache-Control: no-cache, no-store\r\n"
                    "Connection: close\r\n\r\n", bmp_size);
                send(csock, hdr, hlen, 0);
                size_t sent = 0;
                while (sent < bmp_size) {
                    ssize_t s = send(csock, bmp + sent, bmp_size - sent, 0);
                    if (s <= 0) break;
                    sent += s;
                }
                free(bmp);
            }
        }
    } else {
        const char *e = "HTTP/1.1 404 Not Found\r\n"
                        "Connection: close\r\n\r\nNot Found";
        send(csock, e, strlen(e), 0);
    }
    close(csock);
}

static void *thread_func(void *arg) {
    int csock = (int)(intptr_t)arg;
    handle_client(csock);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* 외부에서 호출 가능한 진입점 (game의 main에서 스레드로 실행)          */
/* ------------------------------------------------------------------ */
void fb_server_run(int port) {

    /* FB 접근 확인 */
    int w, h, bpp;
    if (fb_get_info(&w, &h, &bpp) < 0) {
        fprintf(stderr, "[fb_server] FB 정보를 읽을 수 없습니다.\n");
        return;
    }
    printf("[fb_server] FB: %dx%d, %dbpp  http://<보드IP>:%d/\n", w, h, bpp, port);

    int ssock = socket(AF_INET, SOCK_STREAM, 0);
    if (ssock < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(ssock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(ssock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(ssock); return 1;
    }
    if (listen(ssock, 8) < 0) {
        perror("listen"); close(ssock); return 1;
    }

    for (;;) {
        int csock = accept(ssock, NULL, NULL);
        if (csock < 0) continue;

        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_func, (void *)(intptr_t)csock) != 0) {
            handle_client(csock);
        } else {
            pthread_detach(tid);
        }
    }

    close(ssock);
}
