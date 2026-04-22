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
/* 2배 다운스케일해서 VM 수신 데이터량 1/4로 줄임 */
#define FB_SCALE 2

static uint8_t *fb_to_bmp(int w, int h, int bpp, size_t *out_size) {
    int outW = w / FB_SCALE;
    int outH = h / FB_SCALE;
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
    int row_stride = outW * 3;
    int pad = (4 - (row_stride % 4)) % 4;
    int padded_row = row_stride + pad;
    size_t pixel_size = (size_t)padded_row * outH;
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
    dh->width            = outW;
    dh->height           = -outH;  /* top-down */
    dh->planes           = 1;
    dh->bpp              = 24;
    dh->compression      = 0;
    dh->image_size       = (uint32_t)pixel_size;
    dh->x_pixels_per_m   = 0;
    dh->y_pixels_per_m   = 0;
    dh->colors_used      = 0;
    dh->colors_important = 0;

    /* 픽셀 변환 + 2x2 블록 평균 다운스케일 (RGB → BGR for BMP) */
    uint8_t *dst = buf + fh->offset;
    for (int dy = 0; dy < outH; dy++) {
        int sy = dy * FB_SCALE;
        int sy1 = (sy + 1 < h) ? sy + 1 : sy;
        uint8_t *row_dst = dst + (size_t)dy * padded_row;
        for (int dx = 0; dx < outW; dx++) {
            int sx = dx * FB_SCALE;
            int sx1 = (sx + 1 < w) ? sx + 1 : sx;
            unsigned int r = 0, g = 0, b = 0;
            /* 2x2 이웃 4픽셀 평균 */
            int coords[4][2] = { {sy, sx}, {sy, sx1}, {sy1, sx}, {sy1, sx1} };
            for (int k = 0; k < 4; k++) {
                int cy = coords[k][0], cx = coords[k][1];
                uint8_t pr, pg, pb;
                if (bpp == 16) {
                    uint16_t p;
                    memcpy(&p, fb + ((size_t)cy * w + cx) * 2, 2);
                    pr = ((p >> 11) & 0x1F) * 255 / 31;
                    pg = ((p >>  5) & 0x3F) * 255 / 63;
                    pb = ( p        & 0x1F) * 255 / 31;
                } else if (bpp == 32) {
                    const uint8_t *px = fb + ((size_t)cy * w + cx) * 4;
                    pb = px[0]; pg = px[1]; pr = px[2];
                } else {
                    const uint8_t *px = fb + ((size_t)cy * w + cx) * 3;
                    pr = px[0]; pg = px[1]; pb = px[2];
                }
                r += pr; g += pg; b += pb;
            }
            row_dst[dx * 3 + 0] = (uint8_t)(b / 4);
            row_dst[dx * 3 + 1] = (uint8_t)(g / 4);
            row_dst[dx * 3 + 2] = (uint8_t)(r / 4);
        }
        memset(row_dst + row_stride, 0, pad);
    }

    free(fb);
    *out_size = total;
    return buf;
}

/* ------------------------------------------------------------------ */
/* 동시 변환 방지 뮤텍스 (보드 OOM 방지)                                */
/* ------------------------------------------------------------------ */
static pthread_mutex_t g_fb_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    "<img id='fb'>"
    "<div id='info'>connecting...</div>"
    "<script>"
    "var img=document.getElementById('fb');"
    "var info=document.getElementById('info');"
    "var prevURL=null;"
    "function refresh(){"
      "var t0=performance.now();"
      "fetch('/frame').then(function(r){"
        "if(!r.ok)throw new Error(r.status);"
        "return r.blob();"
      "}).then(function(blob){"
        "var url=URL.createObjectURL(blob);"
        "img.src=url;"
        "if(prevURL)URL.revokeObjectURL(prevURL);"
        "prevURL=url;"
        "var ms=Math.round(performance.now()-t0);"
        "var kb=Math.round(blob.size/1024);"
        "info.textContent=new Date().toLocaleTimeString()+'  |  '+ms+' ms  |  '+kb+' KB';"
        "setTimeout(refresh,1000);"
      "}).catch(function(){"
        "info.textContent='error - retrying...';"
        "setTimeout(refresh,3000);"
      "});"
    "}"
    "refresh();"
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
            /* 한 번에 하나씩만 변환 (보드 OOM 방지) */
            pthread_mutex_lock(&g_fb_mutex);
            size_t bmp_size = 0;
            uint8_t *bmp = fb_to_bmp(w, h, bpp, &bmp_size);
            pthread_mutex_unlock(&g_fb_mutex);
            if (!bmp) {
                const char *e = "HTTP/1.1 503 Service Unavailable\r\n"
                                "Connection: close\r\n\r\nbusy";
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
    if (ssock < 0) { perror("socket"); return; }

    int opt = 1;
    setsockopt(ssock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(ssock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(ssock); return;
    }
    if (listen(ssock, 8) < 0) {
        perror("listen"); close(ssock); return;
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
