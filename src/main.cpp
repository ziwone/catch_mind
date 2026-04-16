#include <iostream>
#include "game.h"

int main() {
    std::cout << "=====================================" << std::endl;
    std::cout << "  CatchMind - 출제자 보드" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    CatchMindGame game;
    
        // Framebuffer 디스플레이 초기화
        if (!game.initDisplay()) {
            std::cerr << "디스플레이 초기화 실패!" << std::endl;
            return 1;
        }
    
    game.start();
    
    return 0;
}
