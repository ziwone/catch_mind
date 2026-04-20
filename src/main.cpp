#include <iostream>
#include <pthread.h>
#include "game.h"

/* fb_server 스레드: 게임과 독립적으로 /dev/fb0 를 HTTP로 서빙 */
extern "C" {
    void fb_server_run(int port);  /* fb_server.c 에서 정의 */
}
static void *fb_server_thread(void *arg) {
    fb_server_run((int)(intptr_t)arg);
    return nullptr;
}

int main() {
    std::cout << "=====================================" << std::endl;
    std::cout << "  CatchMind - 출제자 보드" << std::endl;
    std::cout << "=====================================" << std::endl;

    /* FB 모니터 서버를 백그라운드 스레드로 실행 (포트 8080) */
    pthread_t fb_tid;
    pthread_create(&fb_tid, nullptr, fb_server_thread, (void *)(intptr_t)8080);
    pthread_detach(fb_tid);
    std::cout << "FB 모니터: http://<보드IP>:8080/" << std::endl;

    CatchMindGame game;
    
        // Framebuffer 디스플레이 초기화
        if (!game.initDisplay()) {
            std::cerr << "디스플레이 초기화 실패!" << std::endl;
            return 1;
        }
    
    game.start();
    
    return 0;
}
