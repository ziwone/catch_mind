#include "game.h"
#include <algorithm>
#include <chrono>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <ifaddrs.h>
#include <iostream>
#include <linux/input.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

namespace {

namespace ui {
constexpr unsigned int BG_DARK = 0x07060f;
constexpr unsigned int BG_MID = 0x130a24;
constexpr unsigned int CARD = 0x1a1133;
constexpr unsigned int CARD_ALT = 0x261448;
constexpr unsigned int STROKE = 0x4d2c80;
constexpr unsigned int ACCENT = 0x00ffd5;
constexpr unsigned int ACCENT_WARM = 0xffc83d;
constexpr unsigned int TEXT_MAIN = 0xf4f6ff;
constexpr unsigned int TEXT_DIM = 0xa996d0;
constexpr unsigned int P1_ACCENT = 0x32c8ff;
constexpr unsigned int P2_ACCENT = 0xff5ea8;
constexpr unsigned int OK = 0x57ff9a;
constexpr unsigned int NG = 0xff657a;
}

void drawPanelCard(Display *display,
                   int x,
                   int y,
                   int w,
                   int h,
                   unsigned int border,
                   unsigned int fillOuter,
                   unsigned int fillInner) {
    if (display == nullptr || w <= 0 || h <= 0) {
        return;
    }
    display->drawRect(x, y, w, h, border);
    if (w > 6 && h > 6) {
        display->drawRect(x + 2, y + 2, w - 4, h - 4, border);
    }
    if (w > 2 && h > 2) {
        display->drawRect(x + 1, y + 1, w - 2, h - 2, fillOuter);
    }
    if (w > 8 && h > 8) {
        display->drawRect(x + 4, y + 4, w - 8, h - 8, fillInner);
    }
}

void drawTextCentered(Display *display,
                      int centerX,
                      int y,
                      const std::string &text,
                      unsigned int color,
                      int scale) {
    if (display == nullptr) {
        return;
    }
    int width = (int)text.size() * 6 * std::max(1, scale);
    display->drawText(centerX - (width / 2), y, text, color, scale);
}

std::string toDisplayLabel(const std::string &text) {
    static const std::unordered_map<std::string, std::string> labels = {
        {"동물", "ANIMAL"},
        {"과일", "FRUIT"},
        {"사물", "OBJECT"},
        {"고양이", "CAT"},
        {"강아지", "DOG"},
        {"토끼", "RABBIT"},
        {"호랑이", "TIGER"},
        {"코끼리", "ELEPHANT"},
        {"사자", "LION"},
        {"사과", "APPLE"},
        {"바나나", "BANANA"},
        {"포도", "GRAPE"},
        {"오렌지", "ORANGE"},
        {"멜론", "MELON"},
        {"복숭아", "PEACH"},
        {"자동차", "CAR"},
        {"핸드폰", "PHONE"},
        {"의자", "CHAIR"},
        {"시계", "CLOCK"},
        {"책", "BOOK"},
        {"컵", "CUP"},
    };

    auto it = labels.find(text);
    if (it != labels.end()) {
        return it->second;
    }
    return text;
}

}

CatchMindGame::CatchMindGame() {
    std::random_device rd;
    rng.seed(rd());
    myLocalIp = getLocalIpAddress();
    int mappedPlayer = getPlayerNumberFromIp(myLocalIp);
    if (mappedPlayer >= 1 && mappedPlayer <= 3) {
        nodeId = "PLAYER" + std::to_string(mappedPlayer);
    } else {
        nodeId = std::to_string(rd()) + "-" + std::to_string(getpid());
    }

    wordBank["동물"] = {"고양이", "강아지", "토끼", "호랑이", "코끼리", "사자"};
    wordBank["과일"] = {"사과", "바나나", "포도", "오렌지", "멜론", "복숭아"};
    wordBank["사물"] = {"자동차", "핸드폰", "의자", "시계", "책", "컵"};
}

CatchMindGame::~CatchMindGame() {
    closeTouchInput();
    closeRoleSocket();
    if (display != nullptr) {
        delete display;
    }
}

bool CatchMindGame::initDisplay() {
    display = new Display();
    if (!display->init()) {
        return false;
    }

    screenW = display->getWidth();
    screenH = display->getHeight();

    topH = (screenH * 2) / 3;
    bottomY = topH;
    panelH = screenH - topH;

    canvasW = std::max(100, screenW - 40);
    canvasH = std::max(100, topH - 40);
    cursorX = canvasX + (canvasW / 2);
    cursorY = canvasY + (canvasH / 2);
    initTouchInput();
    return true;
}

void CatchMindGame::drawGameLayout() {
    if (display == nullptr) return;

    display->beginFrame();

    display->clearScreen(ui::BG_DARK);

    // 출제자: 좌상단 정보 패널 + 우상단 큰 캔버스
    // 도전자: 기존 레이아웃 (상단 캔버스 + 하단 패널)
    // 출제자/도전자 공통 레이아웃: 좌상단 정보 패널 + 우상단 캔버스
    const int infoPanelW = 150;
    const int padding = 8;

    int topRatioPct = isDrawerRole ? 70 : 56;
    topH = (screenH * topRatioPct) / 100;
    topH = std::max(140, std::min(screenH - 90, topH));
    bottomY = topH;
    panelH = screenH - topH;

    // 캔버스 영역 (우상단, 정보 패널 우측)
    canvasX = infoPanelW + padding;
    canvasY = padding;
    canvasW = std::max(100, screenW - infoPanelW - 2*padding);
    canvasH = std::max(100, topH - 2*padding);

    // 좌상단 정보 패널 배경
    display->drawRect(0, 0, infoPanelW, topH, ui::BG_MID);
    drawPanelCard(display, 2, 2, infoPanelW - 4, topH - 4, ui::STROKE, ui::CARD, 0x0b151f);
    // 정보 패널 헤더
    display->drawRect(4, 4, infoPanelW - 8, 20, ui::STROKE);
    display->drawText(8, 8, "INFO", ui::TEXT_DIM, 1);

    // 캔버스 테두리
    unsigned int canvasAccent = isDrawerRole ? ui::ACCENT_WARM : ui::ACCENT;
    drawPanelCard(display, canvasX - 8, canvasY - 8, canvasW + 16, canvasH + 16,
                  canvasAccent, ui::CARD, 0x0b141c);
    display->drawRect(canvasX, canvasY, canvasW, canvasH, Display::COLOR_BLACK);

    if (isDrawerRole) {
        display->drawRect(canvasX + 8, canvasY + 8, 96, 24, ui::ACCENT_WARM);
        display->drawText(canvasX + 14, canvasY + 14, "DRAWER", 0x2a2110, 1);
    } else {
        display->drawRect(canvasX + 8, canvasY + 8, 122, 24, ui::ACCENT);
        display->drawText(canvasX + 14, canvasY + 14, "CHALLENGER", 0x102822, 1);
    }

    // 커서 위치 보정
    if (cursorX < canvasX || cursorX >= canvasX + canvasW ||
        cursorY < canvasY || cursorY >= canvasY + canvasH) {
        cursorX = canvasX + (canvasW / 2);
        cursorY = canvasY + (canvasH / 2);
    }

    // 하단 도전자 패널
    int halfW = screenW / 2;
    display->drawRect(0, bottomY, screenW, panelH, ui::BG_MID);
    drawPanelCard(display, 0, bottomY, halfW, panelH, ui::P1_ACCENT, ui::CARD_ALT, 0x102336);
    drawPanelCard(display, halfW, bottomY, screenW - halfW, panelH, ui::P2_ACCENT, ui::CARD_ALT, 0x2a1b21);

    display->drawRect(0, bottomY - 3, screenW, 3, ui::STROKE);
    display->drawRect(halfW - 1, bottomY, 2, panelH, ui::STROKE);

    display->drawText(10, bottomY + 8, "P1", ui::TEXT_MAIN, 2);
    display->drawText(halfW + 10, bottomY + 8, "P2", ui::TEXT_MAIN, 2);

    drawStatus();
    display->endFrame();
}

void CatchMindGame::drawStatus() {
    std::cout << "[round " << (round + 1) << "] "
              << "카테고리=" << currentCategory
              << ", 상태=" << (isDrawing ? "진행중" : "대기") << std::endl;
}

void CatchMindGame::start() {
    // TTY 커서 숨기기 (framebuffer 위에 커서 깜빡임 방지)
    printf("\033[?25l");
    fflush(stdout);

    bgm.setVolume(80);
    bgm.play("/mnt/nfs/bgm/maple1.wav");

    std::cout << "=====================================\n";
    std::cout << "캐치마인드 멀티보드 프로토타입\n";
    std::cout << "- 같은 허브의 보드들이 역할을 자동 연동\n";
    std::cout << "- 출제자는 터치로 카테고리/주제어 선택\n";
    std::cout << "=====================================\n";

    initRoleSocket();

    // 게임 시작 전: 전원 READY 합의
    if (!waitForAllPlayersReadyAtStart()) {
        stop();
        return;
    }

    while (round < MAX_ROUNDS) {
        if (!roleSelection()) {
            stop();
            return;
        }

        if (isDrawerRole) {
            if (!selectCategoryAndWord()) {
                continue;
            }
            runSingleBoardRound();
        } else {
            runChallengerStandby();
        }

        round++;
        std::cout << "[시스템] 역할 선택 화면으로 복귀 (라운드 " << round << "/" << MAX_ROUNDS << ")\n\n";
    }

    showFinalScores();
}

void CatchMindGame::stop() {
    // TTY 커서 복원
    printf("\033[?25h");
    fflush(stdout);

    std::cout << "[시스템] 게임 종료\n";
    if (display != nullptr) {
        display->beginFrame();
        display->clearScreen(Display::COLOR_BLACK);
        display->endFrame();
    }
}

bool CatchMindGame::roleSelection() {
    // 정답 후 라운드: isDrawerRole이 이미 세팅된 경우 자동 인정
    if (round > 0 && isDrawerRole && currentDrawerNodeId == nodeId) {
        std::cout << "[역할] 정답자 출제자 자동 인정\n";
        broadcastDrawerSelected();
        return true;
    }
    if (round > 0 && !isDrawerRole) {
        std::cout << "[역할] 이전 정답자가 출제자, 자동 도전자\n";
        return true;
    }

    if (display == nullptr) {
        return false;
    }

    display->beginFrame();
    display->clearScreen(ui::BG_DARK);
    int midX = screenW / 2;
    int boxY = screenH / 3;
    int boxW = screenW / 3;
    int boxH = screenH / 3;
    int gap = 20;
    int drawerX = midX - boxW - gap;
    int challengerX = midX + gap;

    display->drawRect(0, 0, screenW, 58, ui::BG_MID);
    drawTextCentered(display, screenW / 2, boxY - 60, "ROLE SELECT", ui::TEXT_MAIN, 3);
    drawTextCentered(display, screenW / 2, boxY - 24, "LEFT DRAWER  |  RIGHT CHALLENGER", ui::TEXT_DIM, 1);

    drawPanelCard(display, drawerX - 5, boxY - 5, boxW + 10, boxH + 10, ui::ACCENT_WARM, 0x3a2f1a, 0x2a2317);
    drawPanelCard(display, challengerX - 5, boxY - 5, boxW + 10, boxH + 10, ui::ACCENT, 0x1a3a35, 0x15302c);
    drawTextCentered(display, drawerX + (boxW / 2), boxY + (boxH / 2) - 14, "DRAWER", ui::TEXT_MAIN, 3);
    drawTextCentered(display, challengerX + (boxW / 2), boxY + (boxH / 2) - 10, "CHALLENGER", ui::TEXT_MAIN, 2);
    display->endFrame();

    std::cout << "[역할] 좌측 터치=출제자, 우측 터치=도전자\n";
    std::cout << "[역할] 키보드 보조입력: 1(출제자), 2(도전자), q(종료)\n";
    std::cout << "[역할] 다른 보드가 출제자를 먼저 고르면 자동으로 도전자가 됩니다.\n";

    // 화면 진입 시 터치 버퍼 비우기 (이전 화면 잃상 제거)
    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX   = false;
        touchHasY   = false;
    }

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;

        if (touchFd >= 0) {
            FD_SET(touchFd, &readfds);
            if (touchFd > maxfd) {
                maxfd = touchFd;
            }
        }

        if (roleSock >= 0) {
            FD_SET(roleSock, &readfds);
            if (roleSock > maxfd) {
                maxfd = roleSock;
            }
        }

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("select(roleSelection)");
            return false;
        }

        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind;
            std::string value;
            std::string senderIp;
            std::string senderNodeId;
            if (receiveControlMessage(kind, value, senderIp, senderNodeId) && senderNodeId != nodeId) {
                if (kind == "ROLE" && value == "DRAWER") {
                    isDrawerRole = false;
                    drawerIp = senderIp;
                    currentDrawerNodeId = senderNodeId;
                    if (myLocalIp.empty()) myLocalIp = getLocalIpAddress();
                    int myBoardNum = getPlayerNumberFromIp(myLocalIp);
                    int drawerBoardNum = getPlayerNumberFromIp(drawerIp);
                    myPlayerNumber = getChallengerSlotByDrawer(myBoardNum, drawerBoardNum);
                    if (myPlayerNumber == 0) myPlayerNumber = 1;
                    std::cout << "[역할] drawer=" << drawerIp
                              << " myIp=" << myLocalIp
                              << " => challenger P" << myPlayerNumber << "\n";
                    std::cout << "[역할] " << drawerIp << " 보드가 출제자 선택 -> 자동 도전자 전환\n";
                    return true;
                }
            }
        }

        if (touchFd >= 0 && FD_ISSET(touchFd, &readfds)) {
            input_event ev{};
            while (read(touchFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                        touchRawX = ev.value;
                        touchHasX = true;
                    } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                        touchRawY = ev.value;
                        touchHasY = true;
                    }
                } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                    bool wasPressed = touchPressed;
                    touchPressed = (ev.value != 0);

                    // 터치가 떼어지는 순간 한 번만 버튼 판정
                    if (wasPressed && !touchPressed && touchHasX && touchHasY) {
                        int sx = 0;
                        int sy = 0;
                        if (!mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                            continue;
                        }

                        bool onDrawer = (sx >= drawerX && sx < drawerX + boxW && sy >= boxY && sy < boxY + boxH);
                        bool onChallenger =
                            (sx >= challengerX && sx < challengerX + boxW && sy >= boxY && sy < boxY + boxH);

                        if (onDrawer) {
                            isDrawerRole = true;
                            drawerIp.clear();
                            currentDrawerNodeId = nodeId;
                            // ① 먼저 브로드캐스트 (다른 보드가 전환화면 동안 신호를 수신)
                            broadcastDrawerSelected();
                            // ② 그 다음에 전환 화면
                            showTransitionScreen("DRAWER SELECTED", "CHOOSE CATEGORY", 1500);
                            // ③ 터치 잔상 제거를 위한 추가 대기
                            usleep(300000);
                            std::cout << "[역할] 출제자 선택 완료\n";
                            return true;
                        }

                        if (onChallenger) {
                            isDrawerRole = false;
                            drawerIp.clear();
                            currentDrawerNodeId.clear();
                            
                            // 자신의 IP 저장
                            myLocalIp = getLocalIpAddress();
                            std::cout << "[역할] 자신의 IP: " << myLocalIp << "\n";
                            
                            broadcastStatusMessage("CHALLENGER_JOIN");
                            // 300ms 동안 상대방 CHALLENGER_JOIN 수신 여부로 P1/P2 결정
                            {
                                auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
                                while (std::chrono::steady_clock::now() < deadline) {
                                    std::string k2, v2, sip2, snid2;
                                    if (receiveControlMessage(k2, v2, sip2, snid2)) {
                                        if (snid2 != nodeId && k2 == "STATUS" && v2 == "CHALLENGER_JOIN") {
                                            // 상대의 IP로부터 상대 플레이어 번호 얻기
                                            int myNum = getPlayerNumberFromIp(myLocalIp);
                                            int otherNum = getPlayerNumberFromIp(sip2);
                                            myPlayerNumber = (myNum < otherNum) ? 1 : 2;
                                            std::cout << "[역할] P" << myNum << " vs P" << otherNum 
                                                      << " -> 나는 P" << myPlayerNumber << "\n";
                                            break;
                                        }
                                    } else {
                                        usleep(5000);
                                    }
                                }
                            }
                            // 상대가 도전자를 선택하지 않으면 자신이 P1
                            if (myPlayerNumber == 0) {
                                myPlayerNumber = 1;
                            }
                            std::string pLabel = (myPlayerNumber == 1) ? "CHALLENGER P1" : "CHALLENGER P2";
                            showTransitionScreen(pLabel.c_str(), "PLEASE WAIT...", 1000);
                            usleep(300000);
                            std::cout << "[역할] 도전자P" << myPlayerNumber << " 선택 완료\n";
                            return true;
                        }
                    }
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string input;
            if (!std::getline(std::cin, input)) {
                return false;
            }

            if (input == "q") {
                return false;
            }

            if (input == "1") {
                isDrawerRole = true;
                drawerIp.clear();
                currentDrawerNodeId = nodeId;
                broadcastDrawerSelected();
                showTransitionScreen("DRAWER SELECTED", "CHOOSE CATEGORY", 1500);
                std::cout << "[역할] 출제자 선택 완료\n";
                return true;
            }

            if (input == "2") {
                isDrawerRole = false;
                drawerIp.clear();
                currentDrawerNodeId.clear();
                
                // 자신의 IP 저장
                myLocalIp = getLocalIpAddress();
                std::cout << "[역할] 자신의 IP: " << myLocalIp << "\n";
                
                broadcastStatusMessage("CHALLENGER_JOIN");
                {
                    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
                    while (std::chrono::steady_clock::now() < deadline) {
                        std::string k2, v2, sip2, snid2;
                        if (receiveControlMessage(k2, v2, sip2, snid2)) {
                            if (snid2 != nodeId && k2 == "STATUS" && v2 == "CHALLENGER_JOIN") {
                                int myNum = getPlayerNumberFromIp(myLocalIp);
                                int otherNum = getPlayerNumberFromIp(sip2);
                                myPlayerNumber = (myNum < otherNum) ? 1 : 2;
                                std::cout << "[역할] P" << myNum << " vs P" << otherNum 
                                          << " -> 나는 P" << myPlayerNumber << "\n";
                                break;
                            }
                        } else {
                            usleep(5000);
                        }
                    }
                }
                if (myPlayerNumber == 0) {
                    myPlayerNumber = 1;
                }
                std::string pLabel = (myPlayerNumber == 1) ? "CHALLENGER P1" : "CHALLENGER P2";
                showTransitionScreen(pLabel.c_str(), "PLEASE WAIT...", 1000);
                std::cout << "[역할] 도전자P" << myPlayerNumber << " 선택 완료\n";
                return true;
            }

            std::cout << "[역할] 올바른 입력: 1, 2, q\n";
        }
    }
}

void CatchMindGame::runChallengerStandby() {
    isDrawing = false;
    if (display != nullptr) {
        display->beginFrame();
        display->clearScreen(ui::BG_DARK);
        int cx = screenW / 2;
        int cy = screenH / 2;
        drawPanelCard(display,
                      cx - (screenW * 3 / 10),
                      cy - (screenH / 7),
                      screenW * 3 / 5,
                      screenH / 3,
                      ui::ACCENT,
                      ui::CARD,
                      ui::BG_MID);
        drawTextCentered(display, cx, cy - 26, "WORD SELECTING", ui::TEXT_MAIN, 3);
        drawTextCentered(display, cx, cy + 20, "PLEASE WAIT", ui::TEXT_DIM, 2);
        display->endFrame();
    }

    std::cout << "[도전자] 출제자=" << (drawerIp.empty() ? "알 수 없음" : drawerIp) << "\n";
    std::cout << "[도전자] 출제자가 주제어를 고르는 중입니다.\n";

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;

        if (roleSock >= 0) {
            FD_SET(roleSock, &readfds);
            if (roleSock > maxfd) {
                maxfd = roleSock;
            }
        }

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("select(challenger)");
            break;
        }

        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind;
            std::string value;
            std::string senderIp;
            std::string senderNodeId;
            if (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId == nodeId) {
                    continue;
                }
                if (kind == "STATUS") {
                    if (value == "WORD_SELECTING" && display != nullptr) {
                        display->beginFrame();
                        display->clearScreen(ui::BG_DARK);
                        int cx = screenW / 2;
                        int cy = screenH / 2;
                        drawPanelCard(display,
                                      cx - (screenW * 3 / 10),
                                      cy - (screenH / 7),
                                      screenW * 3 / 5,
                                      screenH / 3,
                                      ui::ACCENT,
                                      ui::CARD,
                                      ui::BG_MID);
                        drawTextCentered(display, cx, cy - 26, "WORD SELECTING", ui::TEXT_MAIN, 3);
                        drawTextCentered(display, cx, cy + 20, "PLEASE WAIT", ui::TEXT_DIM, 2);
                        display->endFrame();
                    } else if (value == "DRAWING_START") {
                        std::cout << "[도전자] DRAWING_START 수신! 출제자=" << senderIp << "\n";
                        drawerIp = senderIp;
                        currentDrawerNodeId = senderNodeId;

                        // 최종 안전장치: 라운드 시작 시점에 P번호 확정
                        if (myLocalIp.empty()) myLocalIp = getLocalIpAddress();
                        int myBoardNum = getPlayerNumberFromIp(myLocalIp);
                        int drawerBoardNum = getPlayerNumberFromIp(drawerIp);
                        int slot = getChallengerSlotByDrawer(myBoardNum, drawerBoardNum);
                        if (slot != 0) myPlayerNumber = slot;
                        if (myPlayerNumber == 0) myPlayerNumber = 1;
                        std::cout << "[도전자] 번호 확정: 보드" << myBoardNum
                                  << ", 출제자보드" << drawerBoardNum
                                  << " => P" << myPlayerNumber << "\n";

                        // 출제자와 동일한 전환 화면 표시
                        showTransitionScreen("GAME START", "GET READY!", 1500);
                        std::cout << "[도전자] 게임 화면 진입 시작\n";
                        runChallengerLiveRound();
                        std::cout << "[도전자] runChallengerLiveRound 종료\n";
                        return;
                    }
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                break;
            }
            if (line == "q") {
                break;
            }
        }
    }
}

void CatchMindGame::runChallengerLiveRound() {
    if (display == nullptr) {
        return;
    }

    myAnswerInput.clear();
    receivedAnswer1.clear();
    receivedAnswer2.clear();
    answerReceived1 = false;
    answerReceived2 = false;
    bool submitted = false;
    bool submitLocked = false;

    // 안전장치: 어떤 경로로 들어와도 P0 방지
    if (myPlayerNumber != 1 && myPlayerNumber != 2) {
        if (myLocalIp.empty()) myLocalIp = getLocalIpAddress();
        int myBoardNum = getPlayerNumberFromIp(myLocalIp);
        int drawerBoardNum = getPlayerNumberFromIp(drawerIp);
        int slot = getChallengerSlotByDrawer(myBoardNum, drawerBoardNum);
        myPlayerNumber = (slot == 0) ? 1 : slot;
        std::cout << "[도전자] P번호 안전보정: myIp=" << myLocalIp
                  << ", drawerIp=" << drawerIp
                  << " => P" << myPlayerNumber << "\n";
    }

    // 도전자 화면 레이아웃 (출제자와 동일: 좌상단 정보 패널 + 우상단 캔버스)
    isDrawing = false;
    drawGameLayout();

    // 도전자 타이머: 라운드 진입 시점부터 측정
    auto challengerRoundStart = std::chrono::steady_clock::now();
    const int ROUND_TIMEOUT_SEC = 60;
    int challengerLastSec = -1;

    const int halfW = screenW / 2;
    const int myPanelX = (myPlayerNumber == 1) ? 0 : halfW;
    const int myPanelW = (myPlayerNumber == 1) ? halfW : (screenW - halfW);
    const int btnW = 92;
    const int btnH = 34;
    const int btnX = myPanelX + myPanelW - btnW - 8;
    const int btnY = bottomY + panelH - btnH - 8;
    const int writeX = myPanelX + 8;
    const int writeY = bottomY + 24;
    const int writeW = myPanelW - 16;
    const int writeH = std::max(30, btnY - writeY - 6);
    const int writeMax = 999;

    bool answerInkWritten = false;
    bool answerStrokeActive = false;
    int answerLastX = 0;
    int answerLastY = 0;
    std::vector<std::pair<int, int>> queuedInkPoints;

    auto sendAnswerControl = [&](const std::string &kind, const std::string &value) {
        if (roleSock < 0) return;
        std::string payload = "CM|" + nodeId + "|" + kind + "|" + value;
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(37031);
        to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        sendto(roleSock, payload.c_str(), payload.size(), 0,
               reinterpret_cast<sockaddr *>(&to), sizeof(to));
    };

    auto sendAnswerPoint = [&](int sx, int sy) {
        int nx = ((sx - writeX) * writeMax) / std::max(1, writeW - 1);
        int ny = ((sy - writeY) * writeMax) / std::max(1, writeH - 1);
        nx = std::max(0, std::min(writeMax, nx));
        ny = std::max(0, std::min(writeMax, ny));
        queuedInkPoints.push_back({nx, ny});
    };

    auto sendAnswerUp = [&]() {
        // 스트로크 구분자: 제출 시 A_UP으로 변환 전송
        queuedInkPoints.push_back({-1, -1});
    };

    auto flushQueuedInk = [&]() {
        for (const auto &pt : queuedInkPoints) {
            if (pt.first < 0 || pt.second < 0) {
                sendAnswerControl("A_UP", std::to_string(myPlayerNumber));
            } else {
                sendAnswerControl("A_POINT", std::to_string(myPlayerNumber) + "," + std::to_string(pt.first) + "," + std::to_string(pt.second));
            }
        }
        queuedInkPoints.clear();
    };

    // 상대 도전자 패널에 잉크 스트로크를 렌더링하기 위한 상태
    bool otherStrokeActive = false;
    int otherLastX = 0, otherLastY = 0;

    auto drawOtherAnswerPoint = [&](int slot, int nx, int ny) {
        int hw = screenW / 2;
        int panelX = (slot == 1) ? 0 : hw;
        int panelW = (slot == 1) ? hw : (screenW - hw);
        int areaX = panelX + 8;
        int areaY = bottomY + 24;
        int areaW = std::max(10, panelW - 16);
        int areaH = std::max(10, panelH - 24 - 40);
        int sx = areaX + (nx * std::max(1, areaW - 1)) / 999;
        int sy = areaY + (ny * std::max(1, areaH - 1)) / 999;
        if (!otherStrokeActive) {
            display->drawRect(sx - 2, sy - 2, 5, 5, Display::COLOR_WHITE);
            otherLastX = sx;
            otherLastY = sy;
            otherStrokeActive = true;
            return;
        }
        int dx = sx - otherLastX, dy = sy - otherLastY;
        int steps = std::max(std::abs(dx), std::abs(dy));
        if (steps < 1) steps = 1;
        for (int i = 1; i <= steps; ++i) {
            int px = otherLastX + (dx * i) / steps;
            int py = otherLastY + (dy * i) / steps;
            display->drawRect(px - 2, py - 2, 5, 5, Display::COLOR_WHITE);
        }
        otherLastX = sx;
        otherLastY = sy;
    };

    auto redrawPanels = [&]() {
        paintAnswerPanel(1, 0x102336);
        paintAnswerPanel(2, 0x2a1b21);

        if (myPlayerNumber == 1) {
            display->drawRect(0, bottomY, halfW, panelH, ui::ACCENT_WARM);
        } else {
            display->drawRect(halfW, bottomY, screenW - halfW, panelH, ui::ACCENT_WARM);
        }

        if (myPlayerNumber == 1) {
            display->drawText(8, bottomY + 26, "ME:", ui::TEXT_MAIN, 1);
            display->drawText(40, bottomY + 26, "WRITE HERE", ui::ACCENT_WARM, 1);
            display->drawText(halfW + 8, bottomY + 26,
                              receivedAnswer2.empty() ? "P2: waiting..." : ("P2: " + receivedAnswer2).substr(0, 16),
                              ui::TEXT_MAIN, 1);
        } else {
            display->drawText(halfW + 8, bottomY + 26, "ME:", ui::TEXT_MAIN, 1);
            display->drawText(halfW + 40, bottomY + 26, "WRITE HERE", ui::ACCENT_WARM, 1);
            display->drawText(8, bottomY + 26,
                              receivedAnswer1.empty() ? "P1: waiting..." : ("P1: " + receivedAnswer1).substr(0, 16),
                              ui::TEXT_MAIN, 1);
        }

        drawPanelCard(display, writeX, writeY, writeW, writeH, ui::STROKE, ui::CARD, 0x0b151f);
        display->drawText(writeX + 6, writeY + 6, "TOUCH WRITE", ui::TEXT_DIM, 1);

        unsigned int submitColor = submitLocked ? 0x3a3f44 : (answerInkWritten ? 0x1f5c3b : 0x30483a);
        unsigned int submitEdge = submitLocked ? ui::TEXT_DIM : ui::OK;
        drawPanelCard(display, btnX, btnY, btnW, btnH, submitEdge, submitColor, submitColor);
        display->drawText(btnX + 12, btnY + 10, "SUBMIT", submitEdge, 1);
    };

    auto redrawSubmitOnly = [&]() {
        unsigned int submitColor = submitLocked ? 0x3a3f44 : (answerInkWritten ? 0x1f5c3b : 0x30483a);
        unsigned int submitEdge = submitLocked ? ui::TEXT_DIM : ui::OK;
        drawPanelCard(display, btnX, btnY, btnW, btnH, submitEdge, submitColor, submitColor);
        display->drawText(btnX + 12, btnY + 10, "SUBMIT", submitEdge, 1);
    };

    std::cout << "[도전자P" << myPlayerNumber << "] 그림 수신 시작\n";
    std::cout << "  터치로 본인 패널에 답 작성 후 submit / 키보드: submit, q\n";
    display->beginFrame();
    redrawPanels();
    display->endFrame();

input_phase:
    submitted = false;

    while (!submitted) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;

        if (roleSock >= 0) { FD_SET(roleSock, &readfds); if (roleSock > maxfd) maxfd = roleSock; }
        if (touchFd >= 0)  { FD_SET(touchFd,  &readfds); if (touchFd  > maxfd) maxfd = touchFd; }

        timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 20000;
        int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) { if (errno == EINTR) continue; break; }

        // 도전자 화면 정보 패널 타이머 업데이트 (초 단위)
        {
            auto cnow = std::chrono::steady_clock::now();
            int celapsed = std::chrono::duration_cast<std::chrono::seconds>(cnow - challengerRoundStart).count();
            if (celapsed != challengerLastSec) {
                challengerLastSec = celapsed;
                int remainSec = ROUND_TIMEOUT_SEC - celapsed;
                if (remainSec < 0) remainSec = 0;
                int mm = remainSec / 60;
                int ss = remainSec % 60;
                char timerStr[16];
                snprintf(timerStr, sizeof(timerStr), "%d:%02d", mm, ss);
                unsigned int timerColor = (remainSec <= 10) ? ui::NG : ui::OK;

                display->drawRect(4, 28, 142, 32, ui::CARD);
                display->drawText(10, 32, timerStr, timerColor, 2);
                drawTimerGauge(remainSec, ROUND_TIMEOUT_SEC);

                display->drawRect(4, 62, 142, 16, ui::CARD);
                std::string roundStr = "Round " + std::to_string(round + 1) + "/" + std::to_string(MAX_ROUNDS);
                display->drawText(10, 64, roundStr.c_str(), ui::TEXT_MAIN, 1);
            }
        }

        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind, value, senderIp, senderNodeId;
            while (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId == nodeId) continue;
                bool fromDrawer = currentDrawerNodeId.empty() || senderNodeId == currentDrawerNodeId;
                if ((kind == "DRAW" || kind == "CLEAR" || kind == "STATUS") && !fromDrawer) {
                    continue;
                }

                if (kind == "DRAW") {
                    std::stringstream parse(value);
                    std::string sxStr, syStr, colorStr;
                    if (std::getline(parse, sxStr, ',') && std::getline(parse, syStr, ',') &&
                        std::getline(parse, colorStr, ',')) {
                        try {
                            int nx = std::stoi(sxStr);
                            int ny = std::stoi(syStr);
                            nx = std::max(0, std::min(999, nx));
                            ny = std::max(0, std::min(999, ny));
                            int sx = canvasX + (nx * std::max(1, canvasW - 1)) / 999;
                            int sy = canvasY + (ny * std::max(1, canvasH - 1)) / 999;
                            unsigned int color = (unsigned int)std::stoul(colorStr, nullptr, 16);
                            if (sx >= canvasX && sx < canvasX + canvasW &&
                                sy >= canvasY && sy < canvasY + canvasH) {
                                display->drawRect(sx - 2, sy - 2, 5, 5, color);
                            }
                        } catch (...) {}
                    }
                } else if (kind == "CLEAR") {
                    display->drawRect(canvasX, canvasY, canvasW, canvasH, Display::COLOR_BLACK);
                } else if (kind == "A_POINT") {
                    // 상대 도전자의 잉크 획을 해당 패널에 렌더링
                    std::stringstream parse(value);
                    std::string pnStr, nxStr, nyStr;
                    if (std::getline(parse, pnStr, ',') && std::getline(parse, nxStr, ',') && std::getline(parse, nyStr, ',')) {
                        try {
                            int nx = std::stoi(nxStr);
                            int ny = std::stoi(nyStr);
                            int senderBoardNum = getPlayerNumberFromIp(senderIp);
                            int drawerBoardNum = getPlayerNumberFromIp(drawerIp);
                            int slot = getChallengerSlotByDrawer(senderBoardNum, drawerBoardNum);
                            if (slot != myPlayerNumber && (slot == 1 || slot == 2)) {
                                drawOtherAnswerPoint(slot, nx, ny);
                            }
                        } catch (...) {}
                    }
                } else if (kind == "A_UP") {
                    otherStrokeActive = false;
                } else if (kind == "ANSWER") {
                    auto sep = value.find(':');
                    if (sep != std::string::npos) {
                        int pn = std::stoi(value.substr(0, sep));
                        int senderBoardNum = getPlayerNumberFromIp(senderIp);
                        int drawerBoardNum = getPlayerNumberFromIp(drawerIp);
                        int pnByIp = getChallengerSlotByDrawer(senderBoardNum, drawerBoardNum);
                        if (pnByIp == 1 || pnByIp == 2) pn = pnByIp;
                        std::string ans = value.substr(sep + 1);
                        if (pn == 1) { receivedAnswer1 = ans; answerReceived1 = true; }
                        if (pn == 2) { receivedAnswer2 = ans; answerReceived2 = true; }
                        // redrawPanels() 대신 "SUBMITTED" 표시만 추가 (잉크 획 유지)
                        int hw = screenW / 2;
                        int pnlX = (pn == 1) ? 4 : hw + 4;
                        display->drawRect(pnlX, bottomY + 4, 90, 16, ui::OK);
                        display->drawText(pnlX + 4, bottomY + 6, "SUBMITTED", 0x071a0e, 1);
                        std::cout << "[도전자P" << myPlayerNumber << "] P" << pn << " 답변 수신: " << ans << "\n";
                    }
                } else if (kind == "STATUS" && value.rfind("WRONG_ALL", 0) == 0) {
                    // WRONG_ALL#answer 형식에서 정답 추출
                    std::string revealedAnswer;
                    auto sep = value.find('#');
                    if (sep != std::string::npos) revealedAnswer = value.substr(sep + 1);
                    bgm.playOnce("/mnt/nfs/bgm/incorrect.wav");
                    showTimeUpScreen(revealedAnswer, false);
                    return;
                } else if (kind == "STATUS" && value == "JUDGING_ACTIVE") {
                    // 다른 참여자의 제출을 출제자가 판정 중이면 즉시 submit 잠금
                    submitLocked = true;
                    redrawSubmitOnly();
                } else if (kind == "STATUS" && value == "JUDGING_END") {
                    submitLocked = false;
                    redrawSubmitOnly();
                } else if (kind == "STATUS" && value == "ROUND_END") {
                    std::cout << "[도전자P" << myPlayerNumber << "] 라운드 종료\n";
                    return;
                } else if (kind == "SCORE_DELTA") {
                    auto colon = value.find(':');
                    if (colon != std::string::npos) {
                        try { gameScores[value.substr(0, colon)] += std::stoi(value.substr(colon + 1)); } catch (...) {}
                    }
                }
            }
        }

        if (touchFd >= 0 && FD_ISSET(touchFd, &readfds)) {
            input_event ev{};
            while (read(touchFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                        touchRawX = ev.value;
                        touchHasX = true;
                    } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                        touchRawY = ev.value;
                        touchHasY = true;
                    }

                } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    if (touchPressed && touchHasX && touchHasY) {
                        int sx = 0, sy = 0;
                        if (!mapTouchToScreen(touchRawX, touchRawY, sx, sy)) continue;

                        if (sx >= writeX && sx < writeX + writeW && sy >= writeY && sy < writeY + writeH) {
                            if (!answerStrokeActive) {
                                display->drawRect(sx - 2, sy - 2, 5, 5, Display::COLOR_WHITE);
                                answerLastX = sx;
                                answerLastY = sy;
                                answerStrokeActive = true;
                                sendAnswerPoint(sx, sy);
                            } else {
                                int dx = sx - answerLastX;
                                int dy = sy - answerLastY;
                                int steps = std::max(std::abs(dx), std::abs(dy)) / 2;
                                if (steps < 1) steps = 1;
                                for (int i = 1; i <= steps; ++i) {
                                    int px = answerLastX + (dx * i) / steps;
                                    int py = answerLastY + (dy * i) / steps;
                                    display->drawRect(px - 2, py - 2, 5, 5, Display::COLOR_WHITE);
                                    sendAnswerPoint(px, py);
                                }
                                answerLastX = sx;
                                answerLastY = sy;
                            }
                            if (!answerInkWritten) {
                                answerInkWritten = true;
                                redrawSubmitOnly();
                            }
                        }
                    }
                } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                    bool wasPressed = touchPressed;
                    touchPressed = (ev.value != 0);
                    if (!touchPressed) {
                        answerStrokeActive = false;
                        sendAnswerUp();
                        if (wasPressed && touchHasX && touchHasY) {
                            int sx = 0, sy = 0;
                            if (mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                                if (sx >= btnX && sx < btnX + btnW && sy >= btnY && sy < btnY + btnH) {
                                    if (!submitted && !submitLocked && answerInkWritten) {
                                        flushQueuedInk();
                                        myAnswerInput = "DRAWN";
                                        broadcastAnswer(myPlayerNumber, myAnswerInput);
                                        submitted = true;
                                        std::cout << "[도전자P" << myPlayerNumber << "] 제출: HANDWRITING\n";
                                    }
                                }
                            }
                        }
                    }
                } else if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) {
                    bool wasPressed = touchPressed;
                    touchPressed = (ev.value >= 0);
                    if (wasPressed && !touchPressed) {
                        answerStrokeActive = false;
                        sendAnswerUp();
                        int sx = 0, sy = 0;
                        if (touchHasX && touchHasY && mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                            if (sx >= btnX && sx < btnX + btnW && sy >= btnY && sy < btnY + btnH) {
                                if (!submitted && !submitLocked && answerInkWritten) {
                                    flushQueuedInk();
                                    myAnswerInput = "DRAWN";
                                    broadcastAnswer(myPlayerNumber, myAnswerInput);
                                    submitted = true;
                                    std::cout << "[도전자P" << myPlayerNumber << "] 제출: HANDWRITING\n";
                                }
                            }
                        }
                    }
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string line;
            if (!std::getline(std::cin, line)) break;
            if (line == "q") break;
            if (line == "submit" && !submitted && !submitLocked && answerInkWritten) {
                flushQueuedInk();
                myAnswerInput = "DRAWN";
                broadcastAnswer(myPlayerNumber, myAnswerInput);
                submitted = true;
                std::cout << "[도전자P" << myPlayerNumber << "] 제출: HANDWRITING\n";
            }
        }
    }

    std::cout << "[도전자P" << myPlayerNumber << "] 제출 완료. 판정 대기 중...\n";
    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;
        if (roleSock >= 0) { FD_SET(roleSock, &readfds); if (roleSock > maxfd) maxfd = roleSock; }

        timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 50000;
        int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) { if (errno == EINTR) continue; break; }

        // 대기 단계에서도 타이머 갱신
        {
            auto cnow = std::chrono::steady_clock::now();
            int celapsed = std::chrono::duration_cast<std::chrono::seconds>(cnow - challengerRoundStart).count();
            if (celapsed != challengerLastSec) {
                challengerLastSec = celapsed;
                int remainSec = ROUND_TIMEOUT_SEC - celapsed;
                if (remainSec < 0) remainSec = 0;
                int mm = remainSec / 60, ss = remainSec % 60;
                char timerStr[16];
                snprintf(timerStr, sizeof(timerStr), "%d:%02d", mm, ss);
                unsigned int timerColor = (remainSec <= 10) ? ui::NG : ui::OK;
                display->drawRect(4, 28, 142, 32, ui::CARD);
                display->drawText(10, 32, timerStr, timerColor, 2);
                drawTimerGauge(remainSec, ROUND_TIMEOUT_SEC);
            }
        }

        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind, value, senderIp, senderNodeId;
            while (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId == nodeId) continue;
                if (kind == "STATUS") {
                    if (value.rfind("WRONG_ALL", 0) == 0) {
                        // WRONG_ALL#answer 형식에서 정답 추출
                        std::string revealedAnswer;
                        auto sep = value.find('#');
                        if (sep != std::string::npos) revealedAnswer = value.substr(sep + 1);
                        bgm.playOnce("/mnt/nfs/bgm/incorrect.wav");
                        showTimeUpScreen(revealedAnswer, false);
                        return;
                    } else if (value == "ROUND_END") {
                        std::cout << "[도전자P" << myPlayerNumber << "] 라운드 종료\n";
                        return;
                    } else if ((value == "RETRY_P1" && myPlayerNumber == 1) ||
                               (value == "RETRY_P2" && myPlayerNumber == 2)) {
                        std::cout << "[도전자P" << myPlayerNumber << "] NG 판정 -> 입력창 초기화 후 재작성\n";
                        bgm.playOnce("/mnt/nfs/bgm/incorrect.wav");
                        myAnswerInput.clear();
                        answerInkWritten = false;
                        answerStrokeActive = false;
                        queuedInkPoints.clear();
                        redrawPanels();
                        goto input_phase;
                    } else if (value == "JUDGING_ACTIVE") {
                        submitLocked = true;
                        redrawSubmitOnly();
                    } else if (value == "JUDGING_END") {
                        submitLocked = false;
                        redrawSubmitOnly();
                    } else if (value.rfind("CORRECT_P", 0) == 0) {
                        int winner = std::stoi(value.substr(9));
                        bool isEarlyAnswer = (value.find("#EARLY") != std::string::npos);
                        if (winner == myPlayerNumber) {
                            broadcastScoreDelta(nodeId, isEarlyAnswer ? 3 : 2);  // 도전자 정답 점수
                            bgm.playOnce("/mnt/nfs/bgm/correct.wav");
                            isDrawerRole = true;
                            drawerIp.clear();
                            currentDrawerNodeId = nodeId;
                            showTransitionScreen("CORRECT!", "YOU ARE NEXT DRAWER", 2500);
                        } else {
                            bgm.playOnce("/mnt/nfs/bgm/incorrect.wav");
                            isDrawerRole = false;
                            currentDrawerNodeId = senderNodeId;
                            showTransitionScreen("CORRECT!", "P" + std::to_string(winner) + " WINS", 2500);
                        }
                        return;
                    }
                } else if (kind == "SCORE_DELTA") {
                    auto colon = value.find(':');
                    if (colon != std::string::npos) {
                        try { gameScores[value.substr(0, colon)] += std::stoi(value.substr(colon + 1)); } catch (...) {}
                    }
                } else if (kind == "DRAW") {
                    std::stringstream parse(value);
                    std::string sxStr, syStr, colorStr;
                    if (std::getline(parse, sxStr, ',') && std::getline(parse, syStr, ',') &&
                        std::getline(parse, colorStr, ',')) {
                        try {
                            int nx = std::stoi(sxStr);
                            int ny = std::stoi(syStr);
                            nx = std::max(0, std::min(999, nx));
                            ny = std::max(0, std::min(999, ny));
                            int sx = canvasX + (nx * std::max(1, canvasW - 1)) / 999;
                            int sy = canvasY + (ny * std::max(1, canvasH - 1)) / 999;
                            unsigned int color = (unsigned int)std::stoul(colorStr, nullptr, 16);
                            if (sx >= canvasX && sx < canvasX + canvasW &&
                                sy >= canvasY && sy < canvasY + canvasH)
                                display->drawRect(sx - 2, sy - 2, 5, 5, color);
                        } catch (...) {}
                    }
                } else if (kind == "A_POINT") {
                    std::stringstream parse(value);
                    std::string pnStr, nxStr, nyStr;
                    if (std::getline(parse, pnStr, ',') && std::getline(parse, nxStr, ',') && std::getline(parse, nyStr, ',')) {
                        try {
                            int nx = std::stoi(nxStr);
                            int ny = std::stoi(nyStr);
                            int senderBoardNum = getPlayerNumberFromIp(senderIp);
                            int drawerBoardNum = getPlayerNumberFromIp(drawerIp);
                            int slot = getChallengerSlotByDrawer(senderBoardNum, drawerBoardNum);
                            if (slot != myPlayerNumber && (slot == 1 || slot == 2)) {
                                drawOtherAnswerPoint(slot, nx, ny);
                            }
                        } catch (...) {}
                    }
                } else if (kind == "A_UP") {
                    otherStrokeActive = false;
                } else if (kind == "ANSWER") {
                    auto sep = value.find(':');
                    if (sep != std::string::npos) {
                        int pn = std::stoi(value.substr(0, sep));
                        int senderBoardNum = getPlayerNumberFromIp(senderIp);
                        int drawerBoardNum = getPlayerNumberFromIp(drawerIp);
                        int pnByIp = getChallengerSlotByDrawer(senderBoardNum, drawerBoardNum);
                        if (pnByIp == 1 || pnByIp == 2) pn = pnByIp;
                        std::string ans = value.substr(sep + 1);
                        if (pn == 1) { receivedAnswer1 = ans; answerReceived1 = true; }
                        if (pn == 2) { receivedAnswer2 = ans; answerReceived2 = true; }
                        int hw = screenW / 2;
                        int pnlX = (pn == 1) ? 4 : hw + 4;
                        display->drawRect(pnlX, bottomY + 4, 90, 16, ui::OK);
                        display->drawText(pnlX + 4, bottomY + 6, "SUBMITTED", 0x071a0e, 1);
                    }
                }
            }
        }
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string line;
            if (!std::getline(std::cin, line)) break;
            if (line == "q") break;
        }
    }
}

std::vector<std::string> CatchMindGame::pickRandomWords(const std::vector<std::string> &pool, int count) {
    std::vector<std::string> words = pool;
    std::shuffle(words.begin(), words.end(), rng);
    if ((int)words.size() > count) {
        words.resize(count);
    }
    return words;
}

bool CatchMindGame::showConfirmDialog(const std::string &selectedText) {
    if (display == nullptr) {
        return true;
    }

    display->beginFrame();
    display->clearScreen(ui::BG_DARK);

    int cx = screenW / 2;
    int cy = screenH / 2;

    int bw = screenW * 3 / 4;
    int bh = screenH / 3;
    int bx = cx - bw / 2;
    int by = cy - bh / 2;
    drawPanelCard(display, bx - 4, by - 4, bw + 8, bh + 8, ui::ACCENT_WARM, ui::CARD, ui::BG_MID);

    // 선택된 텍스트 표시
    std::string label = toDisplayLabel(selectedText);
    drawTextCentered(display, cx, by + 24, label, ui::TEXT_MAIN, 3);

    drawTextCentered(display, cx, by + 78, "CONFIRM SELECTION", ui::ACCENT_WARM, 2);

    // 좌측 O / 우측 X 버튼 (터치 오작동 방지 위해 대형화)
    int btnMargin = 16;
    int btnGap = 24;
    int btnW = (screenW - (btnMargin * 2) - btnGap) / 2;
    int btnH = 78;
    int leftBtnX = btnMargin;
    int rightBtnX = btnMargin + btnW + btnGap;
    int btnY = screenH - btnH - 18;

    drawPanelCard(display, leftBtnX - 2, btnY - 2, btnW + 4, btnH + 4, ui::OK, 0x1c492d, 0x1a3e29);
    drawTextCentered(display, leftBtnX + (btnW / 2), btnY + 22, "O (1)", ui::OK, 3);

    drawPanelCard(display, rightBtnX - 2, btnY - 2, btnW + 4, btnH + 4, ui::NG, 0x4f2222, 0x3f1e1e);
    drawTextCentered(display, rightBtnX + (btnW / 2), btnY + 22, "X (2)", ui::NG, 3);
    display->endFrame();

    std::cout << "[확인] " << selectedText << " 를 선택했습니다. (1=확인, 2=취소)\n";

    // 터치/키 입력 대기
    while (true) {
        int sx = 0;
        int sy = 0;
        if (waitTouchReleasePoint(sx, sy, 200)) {
            if (sx >= leftBtnX && sx < leftBtnX + btnW && sy >= btnY && sy < btnY + btnH) {
                std::cout << "[확인] 확인 선택\n";
                return true;
            }
            if (sx >= rightBtnX && sx < rightBtnX + btnW && sy >= btnY && sy < btnY + btnH) {
                std::cout << "[확인] 취소 선택\n";
                return false;
            }
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        if (select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv) > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                return false;
            }
            if (line == "1") {
                std::cout << "[확인] 확인 선택\n";
                return true;
            }
            if (line == "2") {
                std::cout << "[확인] 취소 선택\n";
                return false;
            }
        }
    }
}

bool CatchMindGame::selectCategoryAndWord() {
    // 도전자들이 runChallengerStandby()에 진입할 시간 확보 (자동 전환된 도전자 대비)
    usleep(500000);  // 500ms

    std::vector<std::string> categories;
    for (const auto &kv : wordBank) {
        categories.push_back(kv.first);
    }
    std::sort(categories.begin(), categories.end());

    // 카테고리 선택 루프 (취소할 때까지 반복)
    while (true) {
        int catSelected = -1;
        if (!selectFromTouchMenu("CATEGORY", categories, catSelected, 0x004488, "WORD_SELECTING")) {
            return false;
        }
        if (catSelected < 0 || catSelected >= (int)categories.size()) {
            return false;
        }

        currentCategory = categories[(size_t)catSelected];

        // 카테고리 확인
        if (!showConfirmDialog(currentCategory)) {
            continue;  // 취소 -> 다시 선택
        }
        break;  // 확인 -> 다음 단계
    }

    offeredWords = pickRandomWords(wordBank[currentCategory], 4);

    // 주제어 선택 루프 (취소할 때까지 반복)
    while (true) {
        int wordSelected = -1;
        if (!selectFromTouchMenu("WORD", offeredWords, wordSelected, 0x664400, "WORD_SELECTING")) {
            return false;
        }
        if (wordSelected < 0 || wordSelected >= (int)offeredWords.size()) {
            return false;
        }

        targetWord = offeredWords[(size_t)wordSelected];

        // 주제어 확인
        if (!showConfirmDialog(targetWord)) {
            continue;  // 취소 -> 다시 선택
        }
        break;  // 확인 -> 게임 시작
    }

    std::cout << "[출제자] 선택한 주제어: " << targetWord << "\n";

    // ① 먼저 도전자들에게 신호 전송 (도전자가 신호를 받은 뒤 화면 전환)
    broadcastStatusMessage("DRAWING_START");

    // ② 도전자들이 신호를 수신하고 처리할 여유(500ms)를 준 뒤 출제자 전환 화면 표시
    usleep(500000);
    showTransitionScreen("GAME START", "GET READY!", 1500);

    // ③ 도전자들이 자신의 전환 화면을 다 보고 게임 루프 진입할 시간을 준 뒤 GAME_READY 전송
    broadcastStatusMessage("GAME_READY");
    return true;
}

void CatchMindGame::printRoundGuide() {
    std::cout << "\n[출제자 명령어]\n";
    std::cout << "  touch drag    : 그리기\n";
    std::cout << "  top palette   : 색상 선택 / clear 버튼\n";
    std::cout << "  p             : 펜 on/off\n";
    std::cout << "  c             : 캔버스 초기화\n";
    std::cout << "  1~7           : 빨주노초파남보\n";
    std::cout << "  0             : 흰색\n";
    std::cout << "  ok1/ng1       : P1 정답/오답 판정\n";
    std::cout << "  ok2/ng2       : P2 정답/오답 판정\n";
    std::cout << "  q             : 라운드 종료\n\n";
    std::cout << "[도전자 명령어]\n";
    std::cout << "  answer <답>   : 답 입력\n";
    std::cout << "  submit        : 제출\n\n";
}

// IP 주소를 플레이어 번호로 변환
int CatchMindGame::getPlayerNumberFromIp(const std::string &ip) {
    // 192.168.10.3 = 플레이어 1
    // 192.168.10.4 = 플레이어 2  
    // 192.168.10.5 = 플레이어 3
    if (ip.find("192.168.10.3") != std::string::npos) return 1;
    if (ip.find("192.168.10.4") != std::string::npos) return 2;
    if (ip.find("192.168.10.5") != std::string::npos) return 3;
    return 0;  // 미지정
}

int CatchMindGame::getChallengerSlotByDrawer(int myBoardNum, int drawerBoardNum) {
    if (myBoardNum < 1 || myBoardNum > 3 || drawerBoardNum < 1 || drawerBoardNum > 3) {
        return 0;
    }

    int c1 = 0, c2 = 0;
    for (int n = 1; n <= 3; ++n) {
        if (n == drawerBoardNum) continue;
        if (c1 == 0) c1 = n;
        else c2 = n;
    }

    if (myBoardNum == c1) return 1;
    if (myBoardNum == c2) return 2;
    return 0;
}

void CatchMindGame::runSingleBoardRound() {
    isDrawing = true;
    penDown = true;
    brushColor = Display::COLOR_WHITE;

    receivedAnswer1.clear();
    receivedAnswer2.clear();
    answerReceived1 = false;
    answerReceived2 = false;

    drawGameLayout();
    display->beginFrame();

    player1LatestAnswer.clear();
    player2LatestAnswer.clear();

    paintAnswerPanel(1, 0x102336);
    paintAnswerPanel(2, 0x2a1b21);

    const unsigned int rainbowColors[8] = {
        Display::COLOR_WHITE, // white (default)
        0xFF3030, // red
        0xFF8C1A, // orange
        0xFFD93D, // yellow
        0x57FF7A, // green
        0x3FA7FF, // blue
        0x3A5CFF, // indigo
        0xA24BFF  // violet
    };
    const int swatchSize = 26;
    const int swatchGap = 8;
    const int clearW = 86;
    const int clearH = 32;
    const int toolsY = 2;
    const int paletteW = (8 * swatchSize) + (7 * swatchGap);
    const int toolsX = std::max(8, screenW - paletteW - clearW - 24);
    const int clearX = toolsX + paletteW + 8;

    auto drawDrawerTools = [&]() {
        display->drawRect(toolsX - 6, toolsY - 3, paletteW + clearW + 22, clearH + 7, ui::BG_MID);
        for (int i = 0; i < 8; ++i) {
            int x = toolsX + i * (swatchSize + swatchGap);
            unsigned int edge = (brushColor == rainbowColors[i]) ? ui::ACCENT : ui::STROKE;
            drawPanelCard(display, x, toolsY, swatchSize, swatchSize, edge, rainbowColors[i], rainbowColors[i]);
        }
        drawPanelCard(display, clearX, toolsY, clearW, clearH, ui::ACCENT_WARM, 0x533611, 0x3c280f);
        display->drawText(clearX + 18, toolsY + 12, "CLEAR", ui::ACCENT_WARM, 1);
    };

    auto drawJudgeButtonsFor = [&](int playerNum, bool visible) {
        int halfW = screenW / 2;
        int panelX = (playerNum == 1) ? 0 : halfW;

        // 참가자 패널 내부 상단에 소형 버튼 표시 (캔버스와 겹치지 않음)
        int btnY = bottomY + 8;
        int btnW = 78;
        int btnH = 30;
        int okX = panelX + 10;
        int ngX = okX + btnW + 6;

        if (!visible) {
            display->drawRect(okX, btnY, btnW, btnH, ui::CARD_ALT);
            display->drawRect(ngX, btnY, btnW, btnH, ui::CARD_ALT);
            return;
        }

        drawPanelCard(display, okX, btnY, btnW, btnH, ui::OK, 0x1c492d, 0x1a3e29);
        display->drawText(okX + 24, btnY + 9, "OK", ui::OK, 1);

        drawPanelCard(display, ngX, btnY, btnW, btnH, ui::NG, 0x4f2222, 0x3f1e1e);
        display->drawText(ngX + 24, btnY + 9, "NG", ui::NG, 1);
    };

    auto getJudgeButtonRects = [&](int playerNum, int &okX, int &ngX, int &btnY, int &btnW, int &btnH) {
        int halfW = screenW / 2;
        int panelX = (playerNum == 1) ? 0 : halfW;
        btnY = bottomY + 8;
        btnW = 78;
        btnH = 30;
        okX = panelX + 10;
        ngX = okX + btnW + 6;
    };

    bool answerStrokeActive1 = false;
    bool answerStrokeActive2 = false;
    int answerLastDrawX1 = 0, answerLastDrawY1 = 0;
    int answerLastDrawX2 = 0, answerLastDrawY2 = 0;

    auto drawAnswerPointOnPanel = [&](int playerNum, int nx, int ny) {
        int halfW = screenW / 2;
        int panelX = (playerNum == 1) ? 0 : halfW;
        int panelW = (playerNum == 1) ? halfW : (screenW - halfW);

        int areaX = panelX + 8;
        int areaY = bottomY + 24;
        int areaW = std::max(10, panelW - 16);
        int areaH = std::max(10, panelH - 24 - 40);

        int sx = areaX + (nx * (areaW - 1)) / 999;
        int sy = areaY + (ny * (areaH - 1)) / 999;

        bool &active = (playerNum == 1) ? answerStrokeActive1 : answerStrokeActive2;
        int &lx = (playerNum == 1) ? answerLastDrawX1 : answerLastDrawX2;
        int &ly = (playerNum == 1) ? answerLastDrawY1 : answerLastDrawY2;

        if (!active) {
            display->drawRect(sx - 2, sy - 2, 5, 5, Display::COLOR_WHITE);
            lx = sx;
            ly = sy;
            active = true;
            return;
        }

        int dx = sx - lx;
        int dy = sy - ly;
        int steps = std::max(std::abs(dx), std::abs(dy));
        if (steps < 1) steps = 1;
        for (int i = 1; i <= steps; ++i) {
            int px = lx + (dx * i) / steps;
            int py = ly + (dy * i) / steps;
            display->drawRect(px - 2, py - 2, 5, 5, Display::COLOR_WHITE);
        }
        lx = sx;
        ly = sy;
    };

    std::cout << "[라운드] 그림 그리기 시작\n";
    std::cout << "[라운드] 카테고리=" << currentCategory << "\n";
    std::cout << "[라운드] 주제어=" << targetWord << " (출제자만 확인)\n";
    if (touchFd < 0) {
        std::cout << "[터치] 터치 장치를 찾지 못했습니다.\n";
    } else {
        std::cout << "[터치] 터치 입력 활성화\n";
    }
    broadcastStatusMessage("DRAWING_ACTIVE");
    printRoundGuide();

    drawBrushDot(cursorX, cursorY);
    drawDrawerTools();
    display->endFrame();

    std::string line;
    bool judgingActive = false;
    bool roundEnded = false;

    // 타이머 시작 (lambdas보다 먼저 선언해 score 계산에 사용)
    auto roundStartTime = std::chrono::steady_clock::now();
    const int ROUND_TIMEOUT_SEC = 60;

    auto judgeOk = [&](int playerNum) {
        drawJudgeButtonsFor(playerNum, false);
        bgm.playOnce("/mnt/nfs/bgm/correct.wav");
        // 30초 기준 점수 계산
        int elapsedJudge = (int)std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - roundStartTime).count();
        bool isEarlyAnswer = (elapsedJudge < 30);
        broadcastScoreDelta(nodeId, isEarlyAnswer ? 2 : 1);   // 출제자 점수
        std::string correctMsg = (playerNum == 1) ? "CORRECT_P1" : "CORRECT_P2";
        correctMsg += isEarlyAnswer ? "#EARLY" : "#LATE";
        broadcastStatusMessage(correctMsg);
        isDrawerRole = false;
        drawerIp.clear();
        currentDrawerNodeId.clear();
        broadcastStatusMessage("JUDGING_END");
        broadcastStatusMessage("ROUND_END");
        isDrawing = false;
        roundEnded = true;
    };

    auto judgeNg = [&](int playerNum) {
        bgm.playOnce("/mnt/nfs/bgm/incorrect.wav");
        if (playerNum == 1) {
            receivedAnswer1.clear();
            answerReceived1 = false;
            answerStrokeActive1 = false;
            paintAnswerPanel(1, 0x102336);
            drawJudgeButtonsFor(1, false);
            broadcastStatusMessage("RETRY_P1");
        } else {
            receivedAnswer2.clear();
            answerReceived2 = false;
            answerStrokeActive2 = false;
            paintAnswerPanel(2, 0x2a1b21);
            drawJudgeButtonsFor(2, false);
            broadcastStatusMessage("RETRY_P2");
        }
        judgingActive = false;
        broadcastStatusMessage("JUDGING_END");
    };

    while (isDrawing) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;

        if (touchFd >= 0) {
            FD_SET(touchFd, &readfds);
            if (touchFd > maxfd) maxfd = touchFd;
        }
        if (roleSock >= 0) {
            FD_SET(roleSock, &readfds);
            if (roleSock > maxfd) maxfd = roleSock;
        }

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 5000;

        int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) { std::perror("select"); break; }

        // 타이머 체크: 60초 초과 시 정답 공개
        auto now = std::chrono::steady_clock::now();
        int elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(now - roundStartTime).count();
        if (elapsedSec >= ROUND_TIMEOUT_SEC && !roundEnded) {
            std::cout << "[타이머] 60초 경과 -> 라운드 종료, 정답 공개\n";
            broadcastStatusMessage("JUDGING_ACTIVE");
            broadcastStatusMessage("WRONG_ALL#" + targetWord); // # 구분자로 정답 포함 전송
            broadcastStatusMessage("JUDGING_END");
            broadcastScoreDelta(nodeId, -1);   // timeout: 출제자 -1점
            showTimeUpScreen(targetWord, true);
            broadcastStatusMessage("ROUND_END");
            isDrawing = false;
            roundEnded = true;
        }

        // 좌상단 정보 패널 업데이트 (초 단위로만 갱신해 깜빡임 방지)
        static int lastDisplayedSec = -1;
        if (elapsedSec != lastDisplayedSec) {
            lastDisplayedSec = elapsedSec;

            int remainSec = ROUND_TIMEOUT_SEC - elapsedSec;
            if (remainSec < 0) remainSec = 0;
            int mm = remainSec / 60;
            int ss = remainSec % 60;
            char timerStr[16];
            snprintf(timerStr, sizeof(timerStr), "%d:%02d", mm, ss);
            unsigned int timerColor = (remainSec <= 10) ? ui::NG : ui::OK;

            // 타이머 영역 클리어 후 재렌더
            display->drawRect(4, 28, 142, 32, ui::CARD);
            display->drawText(10, 32, timerStr, timerColor, 2);
            drawTimerGauge(remainSec, ROUND_TIMEOUT_SEC);

            // 라운드 정보
            display->drawRect(4, 62, 142, 16, ui::CARD);
            std::string roundStr = "Round " + std::to_string(round + 1) + "/" + std::to_string(MAX_ROUNDS);
            display->drawText(10, 64, roundStr.c_str(), ui::TEXT_MAIN, 1);

            // 주제어 표시 (출제자 전용)
            display->drawRect(4, 80, 142, 34, 0x1a1133);
            display->drawRect(4, 80, 142, 14, ui::STROKE);
            display->drawText(8, 82, "WORD:", ui::TEXT_DIM, 0);
            std::string wordLabel = toDisplayLabel(targetWord);
            display->drawText(8, 96, wordLabel.substr(0, 12).c_str(), ui::ACCENT_WARM, 1);

            // P1/P2 제출 상태
            int statusY = 118;
            if (answerReceived1) {
                display->drawRect(4, statusY, 142, 16, ui::OK);
                display->drawText(8, statusY + 2, "P1: SUBMITTED", 0x071a0e, 0);
            } else {
                display->drawRect(4, statusY, 142, 16, ui::CARD);
                display->drawText(8, statusY + 2, "P1: waiting", ui::TEXT_DIM, 0);
            }
            statusY += 20;
            if (answerReceived2) {
                display->drawRect(4, statusY, 142, 16, ui::OK);
                display->drawText(8, statusY + 2, "P2: SUBMITTED", 0x071a0e, 0);
            } else {
                display->drawRect(4, statusY, 142, 16, ui::CARD);
                display->drawText(8, statusY + 2, "P2: waiting", ui::TEXT_DIM, 0);
            }
        }

        if (touchFd >= 0 && FD_ISSET(touchFd, &readfds)) {
            bool released = false;
            int rsx = 0, rsy = 0;
            processTouchEvents(&released, &rsx, &rsy);

            // 출제자 화면의 소형 OK/NG 버튼 터치 판정
            if (released) {
                bool toolHandled = false;
                for (int i = 0; i < 8; ++i) {
                    int x = toolsX + i * (swatchSize + swatchGap);
                    if (rsx >= x && rsx < x + swatchSize && rsy >= toolsY && rsy < toolsY + swatchSize) {
                        brushColor = rainbowColors[i];
                        drawDrawerTools();
                        drawBrushDot(cursorX, cursorY);
                        std::cout << "[그리기] 팔레트 색상 선택 index=" << (i + 1) << "\n";
                        toolHandled = true;
                        break;
                    }
                }
                if (toolHandled) {
                    continue;
                }
                if (rsx >= clearX && rsx < clearX + clearW && rsy >= toolsY && rsy < toolsY + clearH) {
                    resetCanvas();
                    drawBrushDot(cursorX, cursorY);
                    drawDrawerTools();
                    broadcastCanvasClear();
                    std::cout << "[그리기] 캔버스 초기화(터치 버튼)\n";
                    continue;
                }

                int okX = 0, ngX = 0, btnY = 0, btnW = 0, btnH = 0;

                getJudgeButtonRects(1, okX, ngX, btnY, btnW, btnH);
                if (answerReceived1 && rsy >= btnY && rsy < btnY + btnH) {
                    if (rsx >= okX && rsx < okX + btnW) { judgeOk(1); continue; }
                    if (rsx >= ngX && rsx < ngX + btnW) { judgeNg(1); continue; }
                }

                getJudgeButtonRects(2, okX, ngX, btnY, btnW, btnH);
                if (answerReceived2 && rsy >= btnY && rsy < btnY + btnH) {
                    if (rsx >= okX && rsx < okX + btnW) { judgeOk(2); continue; }
                    if (rsx >= ngX && rsx < ngX + btnW) { judgeNg(2); continue; }
                }
            }
        }

        // 도전자 답 수신
        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind, value, senderIp, senderNodeId;
            while (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId == nodeId) continue;
                if (kind == "ANSWER") {
                    if (judgingActive) {
                        std::cout << "[출제자] 저지 진행 중이므로 추가 제출 무시\n";
                        continue;
                    }
                    auto sep = value.find(':');
                    if (sep != std::string::npos) {
                        int pn = std::stoi(value.substr(0, sep));
                        std::string ans = value.substr(sep + 1);
                        if (pn == 1) { receivedAnswer1 = ans; answerReceived1 = true; }
                        if (pn == 2) { receivedAnswer2 = ans; answerReceived2 = true; }
                        std::cout << "[출제자] P" << pn << " 답 수신: " << ans << "\n";

                        // 판정 시작 잠금: 다른 도전자의 submit 잠금
                        judgingActive = true;
                        broadcastStatusMessage("JUDGING_ACTIVE");

                        // 화면 전환 없이 현재 3분할 화면에서 판정 버튼 표시
                        drawJudgeButtonsFor(pn, true);
                    }
                } else if (kind == "A_POINT") {
                    std::stringstream parse(value);
                    std::string pnStr, nxStr, nyStr;
                    if (std::getline(parse, pnStr, ',') && std::getline(parse, nxStr, ',') && std::getline(parse, nyStr, ',')) {
                        try {
                            int pn = std::stoi(pnStr);
                            int nx = std::stoi(nxStr);
                            int ny = std::stoi(nyStr);
                            if (pn == 1 || pn == 2) drawAnswerPointOnPanel(pn, nx, ny);
                        } catch (...) {}
                    }
                } else if (kind == "A_UP") {
                    try {
                        int pn = std::stoi(value);
                        if (pn == 1) answerStrokeActive1 = false;
                        if (pn == 2) answerStrokeActive2 = false;
                    } catch (...) {}
                } else if (kind == "STATUS" && value == "ROUND_END") {
                    isDrawing = false;
                    roundEnded = true;
                    break;
                } else if (kind == "SCORE_DELTA") {
                    auto colon = value.find(':');
                    if (colon != std::string::npos) {
                        try { gameScores[value.substr(0, colon)] += std::stoi(value.substr(colon + 1)); } catch (...) {}
                    }
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (!std::getline(std::cin, line)) { isDrawing = false; break; }

            if (line == "q") {
                std::cout << "[라운드] 출제자가 종료\n";
                isDrawing = false;
                roundEnded = true;
                if (judgingActive) broadcastStatusMessage("JUDGING_END");
                broadcastStatusMessage("ROUND_END");
                break;
            }

            if (line == "ok1" && answerReceived1) {
                judgeOk(1);
                break;
            }
            if (line == "ok2" && answerReceived2) {
                judgeOk(2);
                break;
            }
            if (line == "ng1" && answerReceived1) {
                judgeNg(1);
                continue;
            }
            if (line == "ng2" && answerReceived2) {
                judgeNg(2);
                continue;
            }

            handleDrawCommand(line);
        }
    }

    // 항상 동일한 3분할 화면 유지. 라운드 종료 신호만 전송.
    if (!roundEnded) {
        if (judgingActive) broadcastStatusMessage("JUDGING_END");
        broadcastStatusMessage("ROUND_END");
        sleep(1);
    }
}

bool CatchMindGame::handleGuess(int playerIndex, const std::string &answer) {
    std::string normalizedAnswer = normalizeText(answer);
    std::string normalizedTarget = normalizeText(targetWord);

    if (playerIndex == 1) {
        player1LatestAnswer = answer;
    } else {
        player2LatestAnswer = answer;
    }

    bool isCorrect = (normalizedAnswer == normalizedTarget);
    if (isCorrect) {
        std::cout << "[판정] 참가자" << playerIndex << " 정답='" << answer << "' => 정답\n";
        paintAnswerPanel(playerIndex, Display::COLOR_GREEN);
        display->drawRect(canvasX - 3, canvasY - 3, canvasW + 6, canvasH + 6, Display::COLOR_GREEN);
        std::cout << "[라운드] 정답! 다음 라운드 출제자 선택\n";
        
        // 정답자가 다음 출제자가 되도록 설정
        isDrawerRole = true;
        drawerIp.clear();
        currentDrawerNodeId = nodeId;
        
        broadcastStatusMessage("ROUND_END");
        sleep(2);
        return true;
    }

    std::cout << "[판정] 참가자" << playerIndex << " 정답='" << answer << "' => 오답\n";
    paintAnswerPanel(playerIndex, Display::COLOR_RED);
    return false;
}

bool CatchMindGame::handleDrawCommand(const std::string &cmd) {
    if (display == nullptr || cmd.empty()) {
        return false;
    }

    if (cmd == "p") {
        penDown = !penDown;
        std::cout << "[그리기] 펜=" << (penDown ? "on" : "off") << "\n";
        return true;
    }
    if (cmd == "c") {
        resetCanvas();
        drawBrushDot(cursorX, cursorY);
        broadcastCanvasClear();
        std::cout << "[그리기] 캔버스 초기화\n";
        return true;
    }
    if (cmd == "1") {
        brushColor = 0xFF3030;
        return true;
    }
    if (cmd == "2") {
        brushColor = 0xFF8C1A;
        return true;
    }
    if (cmd == "3") {
        brushColor = 0xFFD93D;
        return true;
    }
    if (cmd == "4") {
        brushColor = 0x57FF7A;
        return true;
    }
    if (cmd == "5") {
        brushColor = 0x3FA7FF;
        return true;
    }
    if (cmd == "6") {
        brushColor = 0x3A5CFF;
        return true;
    }
    if (cmd == "7") {
        brushColor = 0xA24BFF;
        return true;
    }
    if (cmd == "0") {
        brushColor = Display::COLOR_WHITE;
        return true;
    }

    std::cout << "[명령어] 알 수 없는 입력\n";
    return false;
}

void CatchMindGame::drawBrushDot(int x, int y) {
    if (display == nullptr) {
        return;
    }
    display->drawRect(x - 2, y - 2, 5, 5, brushColor);
}

void CatchMindGame::resetCanvas() {
    if (display == nullptr) {
        return;
    }
    display->drawRect(canvasX, canvasY, canvasW, canvasH, Display::COLOR_BLACK);
}

void CatchMindGame::paintAnswerPanel(int playerIndex, unsigned int color) {
    if (display == nullptr) {
        return;
    }

    int halfW = screenW / 2;
    int x = (playerIndex == 1) ? 0 : halfW;
    int w = (playerIndex == 1) ? halfW : (screenW - halfW);

    unsigned int edge = (playerIndex == 1) ? ui::P1_ACCENT : ui::P2_ACCENT;
    drawPanelCard(display, x + 3, bottomY + 3, w - 6, panelH - 6, edge, ui::CARD_ALT, color);
}

std::string CatchMindGame::normalizeText(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (!std::isspace(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out;
}

bool CatchMindGame::initTouchInput() {
    closeTouchInput();

    for (int i = 0; i < 16; ++i) {
        std::string dev = "/dev/input/event" + std::to_string(i);
        int fd = open(dev.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        input_absinfo absX{};
        input_absinfo absY{};
        if (ioctl(fd, EVIOCGABS(ABS_X), &absX) == 0 && ioctl(fd, EVIOCGABS(ABS_Y), &absY) == 0) {
            touchFd = fd;
            touchMaxX = (absX.maximum > 0) ? absX.maximum : (screenW - 1);
            touchMaxY = (absY.maximum > 0) ? absY.maximum : (screenH - 1);
            std::cout << "[touch] using " << dev << " max(" << touchMaxX << ", " << touchMaxY << ")\n";
            return true;
        }

        close(fd);
    }

    touchFd = -1;
    return false;
}

void CatchMindGame::closeTouchInput() {
    if (touchFd >= 0) {
        close(touchFd);
        touchFd = -1;
    }
}

bool CatchMindGame::mapTouchToScreen(int rawX, int rawY, int &x, int &y) const {
    if (touchMaxX <= 0 || touchMaxY <= 0 || screenW <= 0 || screenH <= 0) {
        return false;
    }

    x = (rawX * (screenW - 1)) / touchMaxX;
    y = (rawY * (screenH - 1)) / touchMaxY;
    x = std::max(0, std::min(screenW - 1, x));
    y = std::max(0, std::min(screenH - 1, y));
    return true;
}

void CatchMindGame::processTouchEvents(bool *released, int *releaseX, int *releaseY) {
    if (touchFd < 0 || display == nullptr || !isDrawing) {
        return;
    }

    if (released != nullptr) {
        *released = false;
    }

    input_event ev{};
    while (read(touchFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                touchRawX = ev.value;
                touchHasX = true;
            } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                touchRawY = ev.value;
                touchHasY = true;
            } else if (ev.code == ABS_MT_TRACKING_ID) {
                bool wasPressed = touchPressed;
                touchPressed = (ev.value >= 0);
                if (!touchPressed) {
                    strokeActive = false;
                    if (released != nullptr && wasPressed && touchHasX && touchHasY) {
                        int sx = 0, sy = 0;
                        if (mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                            *released = true;
                            if (releaseX != nullptr) *releaseX = sx;
                            if (releaseY != nullptr) *releaseY = sy;
                        }
                    }
                }
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            bool wasPressed = touchPressed;
            touchPressed = (ev.value != 0);
            if (!touchPressed) {
                strokeActive = false;
                if (released != nullptr && wasPressed && touchHasX && touchHasY) {
                    int sx = 0, sy = 0;
                    if (mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                        *released = true;
                        if (releaseX != nullptr) *releaseX = sx;
                        if (releaseY != nullptr) *releaseY = sy;
                    }
                }
            }
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            if (!touchPressed || !penDown || !touchHasX || !touchHasY) {
                continue;
            }

            int sx = 0;
            int sy = 0;
            if (!mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                continue;
            }

            if (sx < canvasX || sx >= canvasX + canvasW || sy < canvasY || sy >= canvasY + canvasH) {
                continue;
            }

            if (!strokeActive) {
                cursorX = sx;
                cursorY = sy;
                strokeLastX = sx;
                strokeLastY = sy;
                strokeActive = true;
                drawBrushDot(cursorX, cursorY);
                broadcastDrawPoint(cursorX, cursorY, brushColor);
            } else {
                int dx = sx - strokeLastX;
                int dy = sy - strokeLastY;
                int steps = std::max(std::abs(dx), std::abs(dy));
                steps = std::max(1, steps);

                for (int i = 1; i <= steps; ++i) {
                    int px = strokeLastX + (dx * i) / steps;
                    int py = strokeLastY + (dy * i) / steps;
                    drawBrushDot(px, py);
                    broadcastDrawPoint(px, py, brushColor);
                }
                cursorX = sx;
                cursorY = sy;
                strokeLastX = sx;
                strokeLastY = sy;
            }
        }
    }
}

bool CatchMindGame::initRoleSocket() {
    closeRoleSocket();

    roleSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (roleSock < 0) {
        std::perror("socket(role)");
        return false;
    }

    int reuse = 1;
    if (setsockopt(roleSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::perror("setsockopt(SO_REUSEADDR)");
        closeRoleSocket();
        return false;
    }

    int broadcast = 1;
    if (setsockopt(roleSock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        std::perror("setsockopt(SO_BROADCAST)");
        closeRoleSocket();
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(37031);
    if (bind(roleSock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::perror("bind(role)");
        closeRoleSocket();
        return false;
    }

    int flags = fcntl(roleSock, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(roleSock, F_SETFL, flags | O_NONBLOCK);
    }

    return true;
}

// 로컬 IP 주소 가져오기 (192.168.10.x 형태)
std::string CatchMindGame::getLocalIpAddress() {
    struct ifaddrs *ifaddr = nullptr, *ifa = nullptr;
    char host[NI_MAXHOST];
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return "";
    }
    
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        
        // IPv4 주소만 처리
        if (ifa->ifa_addr->sa_family == AF_INET) {
            int ret = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), 
                                  host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
            if (ret == 0) {
                std::string ip(host);
                // 고정 규칙 대상 대역만 사용
                if (ip.rfind("192.168.10.", 0) == 0) {
                    freeifaddrs(ifaddr);
                    return ip;
                }
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return "";
}

void CatchMindGame::closeRoleSocket() {
    if (roleSock >= 0) {
        close(roleSock);
        roleSock = -1;
    }
}

// 타임아웃 시 정답 공개 화면 + 전원 확인 대기
// 출제자는 isDrawer=true, 도전자는 false
// 로컬 확인 후 READY_NEXT를 broadcast하고, 나머지 2명의 READY_NEXT를 기다림
void CatchMindGame::showTimeUpScreen(const std::string &answer, bool isDrawer) {
    if (display == nullptr) return;

    display->beginFrame();

    // 정답 공개 화면 그리기
    display->clearScreen(ui::BG_DARK);
    int cx = screenW / 2;
    int cy = screenH / 2;

    int bw = screenW * 3 / 4;
    int bh = screenH / 3;
    int bx = cx - bw / 2;
    int by = cy - bh / 2 - 30;

    drawPanelCard(display, bx - 6, by - 6, bw + 12, bh + 12, ui::NG, ui::CARD, ui::BG_MID);
    drawTextCentered(display, cx, by + 16, "TIME UP!", ui::NG, 3);
    std::string answerLabel = toDisplayLabel(answer);
    drawTextCentered(display, cx, by + bh / 2 + 4, ("ANSWER: " + answerLabel).c_str(), ui::ACCENT, 2);
    drawTextCentered(display, cx, by + bh - 16, isDrawer ? "You were the DRAWER" : "Better luck next time!", ui::TEXT_DIM, 1);

    // 확인 버튼
    int btnW = 180, btnH = 44;
    int btnX = cx - btnW / 2;
    int btnY = by + bh + 24;
    drawPanelCard(display, btnX, btnY, btnW, btnH, ui::OK, 0x1c492d, 0x1a3e29);
    drawTextCentered(display, cx, btnY + 14, isDrawer ? "WAITING CHALLENGERS" : "TAP TO CONFIRM", ui::OK, 1);

    // 대기 상태 텍스트 영역
    int waitY = btnY + btnH + 16;
    display->drawText(cx - 120, waitY, "Tap confirm to continue", ui::TEXT_DIM, 1);
    display->endFrame();

    bool myConfirmed = isDrawer; // 출제자는 로컬 확인 없이 진행
    bool readySent = false;
    int othersReady = 0;
    bool roundEndReceived = false;
    std::vector<std::string> readyNodes;
    const int OTHERS_NEEDED = isDrawer ? 2 : 1; // 출제자: 도전자2명, 도전자: 상대 도전자1명

    auto sendReadyIfNeeded = [&]() {
        if (!isDrawer && myConfirmed && !readySent) {
            broadcastStatusMessage("READY_NEXT");
            readySent = true;
            drawPanelCard(display, btnX, btnY, btnW, btnH, ui::TEXT_DIM, 0x2b3138, 0x2b3138);
            drawTextCentered(display, cx, btnY + 14, "CONFIRMED", ui::TEXT_DIM, 1);
            display->endFrame();
        }
    };

    while ((!myConfirmed || othersReady < OTHERS_NEEDED || (!isDrawer && !readySent)) && !roundEndReceived) {
        sendReadyIfNeeded();

        // 대기 중 표시 업데이트
        display->drawRect(cx - 150, waitY, 300, 24, ui::BG_DARK);
        std::string waitMsg;
        if (!myConfirmed) {
            waitMsg = "Tap button to confirm";
        } else {
            waitMsg = "Waiting for other players... (" + std::to_string(othersReady) + "/" + std::to_string(OTHERS_NEEDED) + ")";
        }
        display->drawText(cx - 140, waitY, waitMsg.c_str(), ui::TEXT_DIM, 1);

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;
        if (roleSock >= 0) { FD_SET(roleSock, &readfds); if (roleSock > maxfd) maxfd = roleSock; }
        if (touchFd >= 0)  { FD_SET(touchFd,  &readfds); if (touchFd  > maxfd) maxfd = touchFd; }

        timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 100000;
        int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) break;

        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind, value, sip, snid;
            while (receiveControlMessage(kind, value, sip, snid)) {
                if (snid == nodeId) continue;
                if (kind == "STATUS" && value == "ROUND_END") {
                    roundEndReceived = true;
                    break;
                }
                if (kind == "STATUS" && value == "READY_NEXT") {
                    if (std::find(readyNodes.begin(), readyNodes.end(), snid) == readyNodes.end()) {
                        readyNodes.push_back(snid);
                        othersReady++;
                        std::cout << "[확인] READY_NEXT 수신 (" << othersReady << "/" << OTHERS_NEEDED << ")\n";
                    }
                }
            }
        }

        if (touchFd >= 0 && FD_ISSET(touchFd, &readfds)) {
            input_event ev{};
            while (read(touchFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                        touchRawX = ev.value;
                        touchHasX = true;
                    } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                        touchRawY = ev.value;
                        touchHasY = true;
                    }
                } else if ((ev.type == EV_KEY && ev.code == BTN_TOUCH) ||
                           (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID)) {
                    bool wasPressed = touchPressed;
                    bool isPressedNow = (ev.type == EV_KEY) ? (ev.value != 0) : (ev.value >= 0);
                    touchPressed = isPressedNow;

                    if (wasPressed && !touchPressed && touchHasX && touchHasY && !myConfirmed) {
                        int sx = 0, sy = 0;
                        if (mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                            if (sx >= btnX && sx < btnX + btnW && sy >= btnY && sy < btnY + btnH) {
                                myConfirmed = true;
                                sendReadyIfNeeded();
                            }
                        }
                    }
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string line;
            if (!std::getline(std::cin, line)) break;
            if (line == "q") break;
            if (!myConfirmed && (line == "confirm" || line == "c" || line == "ok")) {
                myConfirmed = true;
                sendReadyIfNeeded();
            }
        }
    }

    // 모두 확인됨 → 짧은 전환 화면
    display->clearScreen(ui::BG_DARK);
    drawTextCentered(display, cx, screenH / 2 - 10, "ALL CONFIRMED", ui::OK, 2);
    drawTextCentered(display, cx, screenH / 2 + 20, "Next Round...", ui::TEXT_DIM, 1);
    display->endFrame();
    usleep(1200000);
}

void CatchMindGame::showTransitionScreen(const std::string &line1, const std::string &line2, int durationMs) {
    if (display == nullptr) {
        usleep(durationMs * 1000);
        return;
    }

    display->beginFrame();

    display->clearScreen(ui::BG_DARK);

    int cx = screenW / 2;
    int cy = screenH / 2;

    int bw = screenW * 2 / 3;
    int bh = screenH / 4;
    int bx = cx - bw / 2;
    int by = cy - bh / 2;
    unsigned int edge = (line1.find("CORRECT") != std::string::npos) ? ui::OK : ui::ACCENT;
    drawPanelCard(display, bx - 6, by - 6, bw + 12, bh + 12, edge, ui::CARD, ui::BG_MID);

    drawTextCentered(display, cx, by + bh / 4 - 10, line1, ui::TEXT_MAIN, 3);
    drawTextCentered(display, cx, by + bh * 3 / 4 - 7, line2, ui::TEXT_DIM, 2);
    display->endFrame();

    usleep(durationMs * 1000);
}

bool CatchMindGame::waitForGameReady(int timeoutMs) {
    if (roleSock < 0) {
        return false;
    }

    int elapsed = 0;
    while (elapsed < timeoutMs) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(roleSock, &readfds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int ready = select(roleSock + 1, &readfds, nullptr, nullptr, &tv);
        if (ready > 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind;
            std::string value;
            std::string senderIp;
            std::string senderNodeId;
            if (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId != nodeId && kind == "STATUS" && value == "GAME_READY") {
                    std::cout << "[동기화] GAME_READY 수신 -> 게임 화면 진입\n";
                    return true;
                }
            }
        }

        elapsed += 100;
    }

    std::cout << "[동기화] 타임아웃 -> 바로 게임 화면 진입\n";
    return false;
}

bool CatchMindGame::waitForAllPlayersReadyAtStart() {
    if (display == nullptr || roleSock < 0) {
        return false;
    }

    // 고정 구성: 총 3명 (PLAYER1/2/3)
    static constexpr int TOTAL_PLAYERS = 3;

    bool myReady = false;
    std::vector<std::string> readyNodes;
    int lastDrawnCount = -1;  // 실제로 숫자가 바뀔 때만 화면 갱신
    auto markReady = [&](const std::string &nid) -> bool {
        if (std::find(readyNodes.begin(), readyNodes.end(), nid) == readyNodes.end()) {
            readyNodes.push_back(nid);
            return true;  // 새로 추가됨
        }
        return false;  // 이미 있음
    };

    auto drawStartReadyIfChanged = [&]() {
        int cur = (int)readyNodes.size();
        if (cur == lastDrawnCount) return;  // 숫자 변화 없으면 스킵
        lastDrawnCount = cur;
        display->beginFrame();

        // 배경 이미지 (PPM) - deploy.sh에서 PNG→PPM 변환된 파일
        bool hasBg = display->drawPNG("/mnt/nfs/img/main_image.ppm", 0, 0, screenW, screenH);
        if (!hasBg) {
            display->clearScreen(ui::BG_DARK);
        }

        int cx = screenW / 2;

        // 하단 반투명 패널 (READY 버튼 + 카운터 영역)
        int panelH2 = 180;
        int panelY = screenH - panelH2;
        display->drawRect(0, panelY, screenW, panelH2, 0xcc07060f);  // 반투명 어두운 바

        int btnW = 340;
        int btnH = 76;
        int btnX = cx - btnW / 2;
        int btnY = panelY + 20;

        if (myReady) {
            drawPanelCard(display, btnX, btnY, btnW, btnH, ui::TEXT_DIM, 0x2b3138, 0x2b3138);
            drawTextCentered(display, cx, btnY + 27, "READY", ui::TEXT_DIM, 3);
        } else {
            drawPanelCard(display, btnX, btnY, btnW, btnH, ui::OK, 0x1c492d, 0x1a3e29);
            drawTextCentered(display, cx, btnY + 27, "READY", ui::OK, 3);
        }

        std::string countText = "Ready " + std::to_string((int)readyNodes.size()) + "/" + std::to_string(TOTAL_PLAYERS);
        drawTextCentered(display, cx, btnY + btnH + 14, countText, ui::ACCENT_WARM, 2);
        drawTextCentered(display, cx, screenH - 16, "Tap anywhere to READY", ui::TEXT_DIM, 1);
        display->endFrame();
    };

    auto drawStartReady = [&]() { drawStartReadyIfChanged(); };

    // 화면 진입 시 터치 버퍼 비우기
    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX = false;
        touchHasY = false;
    }

    // 네트워크 버퍼 비우기: 500ms 대기 후 flush
    // (이전 게임에서 늦게 도착하는 LOBBY_READY 패킷 제거)
    usleep(500000);
    if (roleSock >= 0) {
        char buffer[256];
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        while (recvfrom(roleSock, buffer, sizeof(buffer), MSG_DONTWAIT,
                       reinterpret_cast<sockaddr *>(&from), &fromLen) > 0) {
        }
    }
    // 진입 시각 기록 - 이 시각 이전에 생성된 LOBBY_READY는 모두 무시됨
    auto lobbyEntryTime = std::chrono::steady_clock::now();

    auto sendReady = [&]() {
        myReady = true;
        markReady(nodeId);
        broadcastStatusMessage("LOBBY_READY");
        std::cout << "[로비] 내가 READY (" << readyNodes.size() << "/" << TOTAL_PLAYERS << ")\n";
        lastDrawnCount = -1;  // 강제 화면 갱신
    };

    drawStartReady();
    auto lastBroadcast = std::chrono::steady_clock::now();

    while (true) {
        if ((int)readyNodes.size() >= TOTAL_PLAYERS) {
            showTransitionScreen("ALL READY", "STARTING...", 900);
            return true;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;
        if (roleSock >= 0) {
            FD_SET(roleSock, &readfds);
            if (roleSock > maxfd) maxfd = roleSock;
        }
        if (touchFd >= 0) {
            FD_SET(touchFd, &readfds);
            if (touchFd > maxfd) maxfd = touchFd;
        }

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return false;
        }

        if (myReady) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBroadcast).count() >= 1000) {
                broadcastStatusMessage("LOBBY_READY");
                lastBroadcast = now;
            }
        }

        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind, value, senderIp, senderNodeId;
            while (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId == nodeId) continue;
                if (kind == "STATUS" && value == "LOBBY_READY") {
                    // 로비 진입 이전에 생성된 잔류 패킷 무시 (1초 여유)
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - lobbyEntryTime).count();
                    if (elapsed < 200) continue;
                    bool isNew = markReady(senderNodeId);
                    if (isNew) {
                        std::cout << "[로비] " << senderNodeId << " READY ("
                                  << readyNodes.size() << "/" << TOTAL_PLAYERS << ")\n";
                        drawStartReady();
                    }
                }
            }
        }

        if (touchFd >= 0 && FD_ISSET(touchFd, &readfds)) {
            input_event ev{};
            while (read(touchFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                // 로비 화면에서는 화면 아무 곳이나 터치하면 READY
                // (버튼이 하나뿐이므로 좌표 판정 불필요)
                auto handleRelease = [&]() {
                    if (!myReady) {
                        std::cout << "[로비] 터치 감지 -> READY\n";
                        sendReady();
                        drawStartReady();
                    }
                };
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                        touchRawX = ev.value; touchHasX = true;
                    } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                        touchRawY = ev.value; touchHasY = true;
                    } else if (ev.code == ABS_MT_TRACKING_ID) {
                        bool wasPressed = touchPressed;
                        touchPressed = (ev.value >= 0);
                        if (wasPressed && !touchPressed) handleRelease();
                    }
                } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                    bool wasPressed = touchPressed;
                    touchPressed = (ev.value != 0);
                    if (wasPressed && !touchPressed) handleRelease();
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string line;
            if (!std::getline(std::cin, line)) return false;
            if (line == "q") return false;
            if (!myReady && (line == "r" || line == "R" || line == "ready")) {
                sendReady();
                drawStartReady();
            }
        }
    }
}

void CatchMindGame::broadcastDrawerSelected() {
    if (roleSock < 0) {
        return;
    }

    std::string payload = "CM|" + nodeId + "|ROLE|DRAWER";

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(37031);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    for (int i = 0; i < 3; ++i) {
        sendto(roleSock,
               payload.c_str(),
               payload.size(),
               0,
               reinterpret_cast<sockaddr *>(&to),
               sizeof(to));
        usleep(50000);
    }
}

void CatchMindGame::broadcastStatusMessage(const std::string &status) {
    if (roleSock < 0) {
        return;
    }

    std::string payload = "CM|" + nodeId + "|STATUS|" + status;

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(37031);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    for (int i = 0; i < 3; ++i) {
        sendto(roleSock,
               payload.c_str(),
               payload.size(),
               0,
               reinterpret_cast<sockaddr *>(&to),
               sizeof(to));
        usleep(50000);
    }
}

void CatchMindGame::broadcastDrawPoint(int x, int y, unsigned int color) {
    if (roleSock < 0) {
        return;
    }

    int nx = ((x - canvasX) * 999) / std::max(1, canvasW - 1);
    int ny = ((y - canvasY) * 999) / std::max(1, canvasH - 1);
    nx = std::max(0, std::min(999, nx));
    ny = std::max(0, std::min(999, ny));

    std::stringstream ss;
    ss << nx << "," << ny << "," << std::hex << color;
    std::string payload = "CM|" + nodeId + "|DRAW|" + ss.str();

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(37031);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    sendto(roleSock,
           payload.c_str(),
           payload.size(),
           0,
           reinterpret_cast<sockaddr *>(&to),
           sizeof(to));
}

void CatchMindGame::broadcastCanvasClear() {
    if (roleSock < 0) {
        return;
    }

    std::string payload = "CM|" + nodeId + "|CLEAR|1";

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(37031);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    for (int i = 0; i < 3; ++i) {
        sendto(roleSock,
               payload.c_str(),
               payload.size(),
               0,
               reinterpret_cast<sockaddr *>(&to),
               sizeof(to));
        usleep(50000);
    }
}

bool CatchMindGame::receiveControlMessage(
    std::string &kind,
    std::string &value,
    std::string &senderIp,
    std::string &senderNodeId) {
    if (roleSock < 0) {
        return false;
    }

    char buf[256] = {0};
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    ssize_t n = recvfrom(roleSock,
                         buf,
                         sizeof(buf) - 1,
                         0,
                         reinterpret_cast<sockaddr *>(&from),
                         &fromLen);
    if (n <= 0) {
        return false;
    }

    std::string msg(buf, static_cast<size_t>(n));

    std::vector<std::string> parts;
    std::stringstream ss(msg);
    std::string token;
    while (std::getline(ss, token, '|')) {
        parts.push_back(token);
    }
    if (parts.size() < 4 || parts[0] != "CM") {
        return false;
    }

    senderNodeId = parts[1];
    kind = parts[2];
    value = parts[3];

    char ip[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip)) != nullptr) {
        senderIp = ip;
    } else {
        senderIp = "unknown";
    }

    return true;
}

bool CatchMindGame::receiveDrawerSelected(std::string &senderIp) {
    std::string kind;
    std::string value;
    std::string senderNodeId;
    if (!receiveControlMessage(kind, value, senderIp, senderNodeId)) {
        return false;
    }
    if (senderNodeId == nodeId) {
        return false;
    }
    return (kind == "ROLE" && value == "DRAWER");
}

bool CatchMindGame::waitTouchReleasePoint(int &sx, int &sy, int timeoutMs) {
    if (touchFd < 0) {
        return false;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(touchFd, &readfds);

    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ready = select(touchFd + 1, &readfds, nullptr, nullptr, &tv);
    if (ready <= 0 || !FD_ISSET(touchFd, &readfds)) {
        return false;
    }

    input_event ev{};
    while (read(touchFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                touchRawX = ev.value;
                touchHasX = true;
            } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                touchRawY = ev.value;
                touchHasY = true;
            } else if (ev.code == ABS_MT_TRACKING_ID) {
                bool wasPressed = touchPressed;
                touchPressed = (ev.value >= 0);
                if (wasPressed && !touchPressed && touchHasX && touchHasY) {
                    return mapTouchToScreen(touchRawX, touchRawY, sx, sy);
                }
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            bool wasPressed = touchPressed;
            touchPressed = (ev.value != 0);
            if (wasPressed && !touchPressed && touchHasX && touchHasY) {
                return mapTouchToScreen(touchRawX, touchRawY, sx, sy);
            }
        }
    }

    return false;
}

bool CatchMindGame::selectFromTouchMenu(const std::string &title,
                                        const std::vector<std::string> &options,
                                        int &selectedIndex,
                                        unsigned int highlightColor,
                                        const std::string &statusForOthers) {
    selectedIndex = -1;
    if (display == nullptr || options.empty()) {
        return false;
    }

    // 이전 화면 터치 잔상 완전 제거
    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX = false;
        touchHasY = false;
        usleep(200000);
    }

    if (!statusForOthers.empty()) {
        broadcastStatusMessage(statusForOthers);
    }

    display->beginFrame();
    display->clearScreen(ui::BG_DARK);
    display->drawRect(0, 0, screenW, 56, ui::BG_MID);
    display->drawText(24, 18, title + " SELECT", ui::TEXT_MAIN, 3);
    display->drawText(screenW - 180, 24, "TOUCH TO CHOOSE", ui::TEXT_DIM, 1);

    const int menuTop = 90;
    const int menuBottom = screenH - 30;
    const int itemGap = 12;
    int itemH = (menuBottom - menuTop - ((int)options.size() - 1) * itemGap) / (int)options.size();
    itemH = std::max(40, itemH);

    for (size_t i = 0; i < options.size(); ++i) {
        int y = menuTop + (int)i * (itemH + itemGap);
        drawPanelCard(display,
                      28,
                      y,
                      screenW - 56,
                      itemH,
                      ui::STROKE,
                      ui::CARD,
                      highlightColor);

        std::string line = std::to_string((int)i + 1) + ". " + toDisplayLabel(options[i]);
        display->drawText(48, y + std::max(8, (itemH / 2) - 10), line, ui::TEXT_MAIN, 2);

        std::cout << "  " << (i + 1) << ") " << options[i] << " [" << toDisplayLabel(options[i]) << "]\n";
    }
    display->endFrame();

    std::cout << "[터치] " << title << " 선택 대기 중...\n";

    while (true) {
        int sx = 0;
        int sy = 0;
        if (waitTouchReleasePoint(sx, sy, 200)) {
            for (size_t i = 0; i < options.size(); ++i) {
                int y = menuTop + (int)i * (itemH + itemGap);
                if (sx >= 30 && sx < (screenW - 30) && sy >= y && sy < (y + itemH)) {
                    selectedIndex = (int)i;
                    return true;
                }
            }
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 1;
        if (select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv) > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                return false;
            }
            if (line == "q") {
                return false;
            }
            try {
                int idx = std::stoi(line) - 1;
                if (idx >= 0 && idx < (int)options.size()) {
                    selectedIndex = idx;
                    return true;
                }
            } catch (...) {
            }
        }

        if (roleSock >= 0) {
            fd_set rolefds;
            FD_ZERO(&rolefds);
            FD_SET(roleSock, &rolefds);
            timeval roleTv{};
            roleTv.tv_sec = 0;
            roleTv.tv_usec = 1;
            if (select(roleSock + 1, &rolefds, nullptr, nullptr, &roleTv) > 0 && FD_ISSET(roleSock, &rolefds)) {
                std::string kind;
                std::string value;
                std::string senderIp;
                std::string senderNodeId;
                if (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                    if (senderNodeId != nodeId && kind == "ROLE" && value == "DRAWER") {
                        isDrawerRole = false;
                        drawerIp = senderIp;
                        currentDrawerNodeId = senderNodeId;
                        return false;
                    }
                }
            }
        }
    }
}

// ─── 터치 키패드 레이아웃 ─────────────────────────────────────────
// QWERTY 3줄 + DEL/CLR/SUBMIT 행
static const char *const KEYPAD_ROWS[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};

void CatchMindGame::drawChallengerKeypad(const std::string &currentInput) {
    if (display == nullptr) return;

    int drawAreaH = (screenH * 7) / 10;
    int inputAreaY = drawAreaH;
    int inputAreaH = screenH - drawAreaH;

    // 배경 다시 그려서 이전 내용 지우기
    display->drawRect(0, inputAreaY, screenW, inputAreaH, ui::BG_MID);
    display->drawRect(0, inputAreaY, screenW, 2, ui::ACCENT);

    // 현재 입력값 표시 박스
    int dispH = 32;
    int dispY = inputAreaY + 4;
    drawPanelCard(display, 4, dispY, screenW - 130, dispH, ui::STROKE, ui::CARD, ui::CARD_ALT);
    if (!currentInput.empty()) {
        display->drawText(10, dispY + 8, currentInput, ui::TEXT_MAIN, 2);
    } else {
        display->drawText(10, dispY + 8, "...", ui::TEXT_DIM, 2);
    }

    // SUBMIT 버튼
    int subBtnW = 120, subBtnH = 36;
    int subBtnX = screenW - subBtnW - 8;
    int subBtnY = inputAreaY + inputAreaH - subBtnH - 8;
    unsigned int subColor = currentInput.empty() ? 0x6f8f7c : ui::OK;
    drawPanelCard(display,
                  subBtnX - 2,
                  subBtnY - 2,
                  subBtnW + 4,
                  subBtnH + 4,
                  subColor,
                  currentInput.empty() ? 0x274132 : 0x1c492d,
                  currentInput.empty() ? 0x223a2c : 0x1a3e29);
    display->drawText(subBtnX + 15, subBtnY + 10, "SUBMIT", subColor, 2);

    // 키패드 그리기
    int keyAreaY = dispY + dispH + 4;
    int keyAreaH = subBtnY - keyAreaY - 4;
    int numRows = 3;
    int keyH = keyAreaH / numRows - 2;
    if (keyH < 12) keyH = 12;

    for (int row = 0; row < numRows; ++row) {
        std::string keys = KEYPAD_ROWS[row];
        int n = (int)keys.size();
        int totalW = screenW - 8;
        int keyW = totalW / n;
        int startX = 4 + (totalW - keyW * n) / 2;
        int y = keyAreaY + row * (keyH + 2);

        for (int col = 0; col < n; ++col) {
            int x = startX + col * keyW;
            drawPanelCard(display, x + 1, y + 1, keyW - 2, keyH - 2, ui::STROKE, ui::CARD, ui::CARD_ALT);
            char ch[2] = {keys[col], 0};
            display->drawText(x + keyW / 2 - 4, y + keyH / 2 - 7, ch, ui::TEXT_MAIN, 1);
        }
    }

    // DEL / CLR 버튼
    int utilBtnH = keyH;
    int utilY = keyAreaY + numRows * (keyH + 2);
    int delX = screenW - 148;
    int clrX = screenW - 74;
    int utilBtnW = 68;

    drawPanelCard(display, delX + 1, utilY + 1, utilBtnW - 2, utilBtnH - 2, ui::NG, 0x4f2222, 0x3f1e1e);
    display->drawText(delX + 8, utilY + 4, "DEL", ui::NG, 1);
    drawPanelCard(display, clrX + 1, utilY + 1, utilBtnW - 2, utilBtnH - 2, ui::ACCENT_WARM, 0x4f3a20, 0x3f311d);
    display->drawText(clrX + 8, utilY + 4, "CLR", ui::ACCENT_WARM, 1);
}

bool CatchMindGame::handleKeypadTouch(int sx, int sy, std::string &input) {
    if (display == nullptr) return false;

    int drawAreaH = (screenH * 7) / 10;
    int inputAreaY = drawAreaH;
    int inputAreaH = screenH - drawAreaH;

    int dispH = 32;
    int dispY = inputAreaY + 4;

    int keyAreaY = dispY + dispH + 4;
    int subBtnH = 36;
    int subBtnY = inputAreaY + inputAreaH - subBtnH - 8;
    int keyAreaH = subBtnY - keyAreaY - 4;
    int numRows = 3;
    int keyH = keyAreaH / numRows - 2;
    if (keyH < 12) keyH = 12;

    for (int row = 0; row < numRows; ++row) {
        std::string keys = KEYPAD_ROWS[row];
        int n = (int)keys.size();
        int totalW = screenW - 8;
        int keyW = totalW / n;
        int startX = 4 + (totalW - keyW * n) / 2;
        int y = keyAreaY + row * (keyH + 2);

        if (sy >= y && sy < y + keyH) {
            for (int col = 0; col < n; ++col) {
                int x = startX + col * keyW;
                if (sx >= x && sx < x + keyW) {
                    input += keys[col];
                    return true;
                }
            }
        }
    }

    // DEL / CLR 확인
    int utilY = keyAreaY + numRows * (keyH + 2);
    int utilBtnH = keyH;
    int utilBtnW = 68;
    int delX = screenW - 148;
    int clrX = screenW - 74;

    if (sy >= utilY && sy < utilY + utilBtnH) {
        if (sx >= delX && sx < delX + utilBtnW) {
            if (!input.empty()) input.pop_back();
            return true;
        }
        if (sx >= clrX && sx < clrX + utilBtnW) {
            input.clear();
            return true;
        }
    }

    return false;
}

void CatchMindGame::broadcastAnswer(int playerNum, const std::string &answer) {
    if (roleSock < 0) return;

    std::string payload = "CM|" + nodeId + "|ANSWER|" +
                          std::to_string(playerNum) + ":" + answer;

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(37031);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    for (int i = 0; i < 3; ++i) {
        sendto(roleSock, payload.c_str(), payload.size(), 0,
               reinterpret_cast<sockaddr *>(&to), sizeof(to));
        usleep(30000);
    }
    std::cout << "[ANSWER 송신] P" << playerNum << " -> " << answer << "\n";
}

// 도전자: 입력 화면 그리기 (P1/P2 이중 패널)
void CatchMindGame::drawChallengerAnswerScreen(int playerNum, const std::string &myInput, 
                                               const std::string &answer1, const std::string &answer2) {
    if (display == nullptr) return;

    int drawAreaH = (screenH * 7) / 10;
    int inputAreaY = drawAreaH;
    int inputAreaH = screenH - drawAreaH;
    int halfW = screenW / 2;

    // 상단 70%: 그림 영역 (캔버스는 이미 그려져 있으므로 경계선만)
    drawPanelCard(display, canvasX - 5, canvasY - 5, canvasW + 10, canvasH + 10, ui::ACCENT, ui::CARD, 0x0b141c);

    // 하단 30%: P1/P2 입력 영역
    // P1 (왼쪽)
    drawPanelCard(display, 0, inputAreaY, halfW, inputAreaH, ui::P1_ACCENT, ui::CARD, 0x102336);
    display->drawText(5, inputAreaY + 5, "P1", ui::P1_ACCENT, 2);
    if (playerNum == 1) {
        display->drawText(5, inputAreaY + 35, "Your Answer:", Display::COLOR_WHITE, 1);
        if (myInput.length() > 0)
            display->drawText(5, inputAreaY + 55, myInput.substr(0, 14), ui::ACCENT_WARM, 1);
        else
            display->drawText(5, inputAreaY + 55, "_____", ui::TEXT_DIM, 1);
    } else {
        if (!answer1.empty())
            display->drawText(5, inputAreaY + 35, answer1.substr(0, 16), ui::ACCENT_WARM, 2);
        else
            display->drawText(5, inputAreaY + 35, "waiting...", ui::TEXT_DIM, 2);
    }

    // P2 (오른쪽)
    drawPanelCard(display, halfW, inputAreaY, halfW, inputAreaH, ui::P2_ACCENT, ui::CARD, 0x2a1b21);
    display->drawText(halfW + 5, inputAreaY + 5, "P2", ui::P2_ACCENT, 2);
    if (playerNum == 2) {
        display->drawText(halfW + 5, inputAreaY + 35, "Your Answer:", Display::COLOR_WHITE, 1);
        if (myInput.length() > 0)
            display->drawText(halfW + 5, inputAreaY + 55, myInput.substr(0, 14), ui::ACCENT_WARM, 1);
        else
            display->drawText(halfW + 5, inputAreaY + 55, "_____", ui::TEXT_DIM, 1);
    } else {
        if (!answer2.empty())
            display->drawText(halfW + 5, inputAreaY + 35, answer2.substr(0, 16), ui::ACCENT_WARM, 2);
        else
            display->drawText(halfW + 5, inputAreaY + 35, "waiting...", ui::TEXT_DIM, 2);
    }

    // SUBMIT 버튼 (자신의 영역에만)
    int btnW = 80, btnH = 30;
    int btnY = inputAreaY + inputAreaH - btnH - 5;
    if (playerNum == 1) {
        drawPanelCard(display, 5, btnY, btnW, btnH, ui::OK, 0x1c492d, 0x1a3e29);
        display->drawText(10, btnY + 8, "SUBMIT", ui::OK, 1);
    } else {
        drawPanelCard(display, halfW + halfW/2 - btnW/2, btnY, btnW, btnH, ui::OK, 0x1c492d, 0x1a3e29);
        display->drawText(halfW + halfW/2 - btnW/2 + 10, btnY + 8, "SUBMIT", ui::OK, 1);
    }
}

// 도전자: 하단 입력 패널만 갱신 (화면 깔박임 방지)
void CatchMindGame::drawAnswerPanelOnly(int playerNum, const std::string &myInput,
                                        const std::string &answer1, const std::string &answer2) {
    if (display == nullptr) return;

    int drawAreaH = (screenH * 7) / 10;
    int inputAreaY = drawAreaH;
    int inputAreaH = screenH - drawAreaH;
    int halfW = screenW / 2;

    drawPanelCard(display, 0, inputAreaY, halfW, inputAreaH, ui::P1_ACCENT, ui::CARD, 0x102336);
    display->drawText(5, inputAreaY + 5, "P1", ui::P1_ACCENT, 2);
    if (playerNum == 1) {
        display->drawText(5, inputAreaY + 35, "Your Answer:", Display::COLOR_WHITE, 1);
        if (myInput.length() > 0)
            display->drawText(5, inputAreaY + 55, myInput.substr(0, 14), ui::ACCENT_WARM, 1);
        else
            display->drawText(5, inputAreaY + 55, "_____", ui::TEXT_DIM, 1);
    } else {
        if (!answer1.empty())
            display->drawText(5, inputAreaY + 35, answer1.substr(0, 16), ui::ACCENT_WARM, 2);
        else
            display->drawText(5, inputAreaY + 35, "waiting...", ui::TEXT_DIM, 2);
    }

    drawPanelCard(display, halfW, inputAreaY, halfW, inputAreaH, ui::P2_ACCENT, ui::CARD, 0x2a1b21);
    display->drawText(halfW + 5, inputAreaY + 5, "P2", ui::P2_ACCENT, 2);
    if (playerNum == 2) {
        display->drawText(halfW + 5, inputAreaY + 35, "Your Answer:", Display::COLOR_WHITE, 1);
        if (myInput.length() > 0)
            display->drawText(halfW + 5, inputAreaY + 55, myInput.substr(0, 14), ui::ACCENT_WARM, 1);
        else
            display->drawText(halfW + 5, inputAreaY + 55, "_____", ui::TEXT_DIM, 1);
    } else {
        if (!answer2.empty())
            display->drawText(halfW + 5, inputAreaY + 35, answer2.substr(0, 16), ui::ACCENT_WARM, 2);
        else
            display->drawText(halfW + 5, inputAreaY + 35, "waiting...", ui::TEXT_DIM, 2);
    }

    // SUBMIT 버튼 (자신의 영역에만)
    int btnW = 80, btnH = 30;
    int btnY = inputAreaY + inputAreaH - btnH - 5;
    if (playerNum == 1) {
        drawPanelCard(display, 5, btnY, btnW, btnH, ui::OK, 0x1c492d, 0x1a3e29);
        display->drawText(10, btnY + 8, "SUBMIT", ui::OK, 1);
    } else {
        drawPanelCard(display, halfW + halfW/2 - btnW/2, btnY, btnW, btnH, ui::OK, 0x1c492d, 0x1a3e29);
        display->drawText(halfW + halfW/2 - btnW/2 + 10, btnY + 8, "SUBMIT", ui::OK, 1);
    }
}

// 출제자: 도전자 답 수신 후 OK/NG 판정 화면
void CatchMindGame::showDrawerJudgeScreen() {
    if (display == nullptr) return;

    auto drawJudge = [&]() {
        display->beginFrame();
        display->clearScreen(ui::BG_DARK);
        display->drawRect(0, 0, screenW, 52, ui::BG_MID);
        display->drawText(20, 10, "JUDGE ANSWERS", ui::ACCENT_WARM, 3);

        int halfW = screenW / 2;
        int panelTop = 60;
        int panelH2 = (screenH - panelTop - 80) / 2;

        // P1 패널
        drawPanelCard(display, 4, panelTop, halfW - 8, panelH2, ui::P1_ACCENT, ui::CARD, 0x102336);
        display->drawText(10, panelTop + 6, "P1:", ui::TEXT_MAIN, 2);
        if (!receivedAnswer1.empty())
            display->drawText(10, panelTop + 30, receivedAnswer1.substr(0, 18), ui::ACCENT_WARM, 2);
        else
            display->drawText(10, panelTop + 30, "waiting...", ui::TEXT_DIM, 2);

        // P2 패널
        drawPanelCard(display, halfW + 4, panelTop, halfW - 8, panelH2, ui::P2_ACCENT, ui::CARD, 0x2a1b21);
        display->drawText(halfW + 10, panelTop + 6, "P2:", ui::TEXT_MAIN, 2);
        if (!receivedAnswer2.empty())
            display->drawText(halfW + 10, panelTop + 30, receivedAnswer2.substr(0, 18), ui::ACCENT_WARM, 2);
        else
            display->drawText(halfW + 10, panelTop + 30, "waiting...", ui::TEXT_DIM, 2);

        int btnY = screenH - 70;
        int btnW = (halfW - 16) / 2;

        // P1 OK/NG
        if (!receivedAnswer1.empty()) {
            drawPanelCard(display, 4, btnY, btnW, 50, ui::OK, 0x1c492d, 0x1a3e29);
            display->drawText(4 + 8, btnY + 14, "P1 OK", ui::OK, 2);
            drawPanelCard(display, 4 + btnW + 4, btnY, btnW, 50, ui::NG, 0x4f2222, 0x3f1e1e);
            display->drawText(4 + btnW + 12, btnY + 14, "P1 NG", ui::NG, 2);
        }
        // P2 OK/NG
        if (!receivedAnswer2.empty()) {
            int p2Base = halfW + 4;
            drawPanelCard(display, p2Base, btnY, btnW, 50, ui::OK, 0x1c492d, 0x1a3e29);
            display->drawText(p2Base + 8, btnY + 14, "P2 OK", ui::OK, 2);
            drawPanelCard(display, p2Base + btnW + 4, btnY, btnW, 50, ui::NG, 0x4f2222, 0x3f1e1e);
            display->drawText(p2Base + btnW + 12, btnY + 14, "P2 NG", ui::NG, 2);
        }
        display->endFrame();
    };

    drawJudge();
    std::cout << "[출제자] 답 판정 화면. 키보드: ok1/ng1/ok2/ng2  q=강제종료\n";

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;
        if (roleSock >= 0) { FD_SET(roleSock, &readfds); if (roleSock > maxfd) maxfd = roleSock; }
        if (touchFd >= 0)  { FD_SET(touchFd,  &readfds); if (touchFd  > maxfd) maxfd = touchFd; }

        timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 50000;
        select(maxfd + 1, &readfds, nullptr, nullptr, &tv);

        // 새 답 수신 감시
        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind, value, senderIp, senderNodeId;
            while (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId == nodeId) continue;
                if (kind == "ANSWER") {
                    // value = "1:고양이" 또는 "2:강아지"
                    auto sep = value.find(':');
                    if (sep != std::string::npos) {
                        int pn = std::stoi(value.substr(0, sep));
                        std::string ans = value.substr(sep + 1);
                        if (pn == 1) { receivedAnswer1 = ans; answerReceived1 = true; }
                        if (pn == 2) { receivedAnswer2 = ans; answerReceived2 = true; }
                        std::cout << "[출제자] P" << pn << " 답 수신: " << ans << "\n";
                        drawJudge();
                    }
                }
            }
        }

        // 터치 판정
        if (touchFd >= 0 && FD_ISSET(touchFd, &readfds)) {
            int sx = 0, sy = 0;
            if (waitTouchReleasePoint(sx, sy, 20)) {
                int halfW = screenW / 2;
                int btnW = (halfW - 16) / 2;
                int btnY = screenH - 70;

                // P1 OK
                if (!receivedAnswer1.empty() && sy >= btnY && sy < btnY + 50 &&
                    sx >= 4 && sx < 4 + btnW) {
                    broadcastStatusMessage("CORRECT_P1");
                    // 출제자는 이번 라운드 끝 후 도전자로 전환
                    isDrawerRole = false;
                    drawerIp.clear();
                    currentDrawerNodeId.clear();
                    broadcastStatusMessage("ROUND_END");
                    showTransitionScreen("P1 CORRECT!", "NEXT ROUND", 2000);
                    return;
                }
                // P1 NG
                if (!receivedAnswer1.empty() && sy >= btnY && sy < btnY + 50 &&
                    sx >= 4 + btnW + 4 && sx < 4 + btnW * 2 + 4) {
                    paintAnswerPanel(1, Display::COLOR_RED);
                    receivedAnswer1.clear(); answerReceived1 = false;
                    drawJudge();
                }
                // P2 OK
                int p2Base = halfW + 4;
                if (!receivedAnswer2.empty() && sy >= btnY && sy < btnY + 50 &&
                    sx >= p2Base && sx < p2Base + btnW) {
                    broadcastStatusMessage("CORRECT_P2");
                    isDrawerRole = false;
                    drawerIp.clear();
                    currentDrawerNodeId.clear();
                    broadcastStatusMessage("ROUND_END");
                    showTransitionScreen("P2 CORRECT!", "NEXT ROUND", 2000);
                    return;
                }
                // P2 NG
                if (!receivedAnswer2.empty() && sy >= btnY && sy < btnY + 50 &&
                    sx >= p2Base + btnW + 4 && sx < p2Base + btnW * 2 + 4) {
                    paintAnswerPanel(2, Display::COLOR_RED);
                    receivedAnswer2.clear(); answerReceived2 = false;
                    drawJudge();
                }
            }
        }

        // 키보드 판정
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string line;
            if (!std::getline(std::cin, line)) break;
            if (line == "q") { broadcastStatusMessage("ROUND_END"); break; }
            if (line == "ok1" && !receivedAnswer1.empty()) {
                broadcastStatusMessage("CORRECT_P1");
                isDrawerRole = false;
                drawerIp.clear();
                currentDrawerNodeId.clear();
                broadcastStatusMessage("ROUND_END");
                showTransitionScreen("P1 CORRECT!", "NEXT ROUND", 2000);
                return;
            }
            if (line == "ng1") { receivedAnswer1.clear(); answerReceived1 = false; drawJudge(); }
            if (line == "ok2" && !receivedAnswer2.empty()) {
                broadcastStatusMessage("CORRECT_P2");
                isDrawerRole = false;
                drawerIp.clear();
                currentDrawerNodeId.clear();
                broadcastStatusMessage("ROUND_END");
                showTransitionScreen("P2 CORRECT!", "NEXT ROUND", 2000);
                return;
            }
            if (line == "ng2") { receivedAnswer2.clear(); answerReceived2 = false; drawJudge(); }
        }
    }
}

// ---------------------------------------------------------------------------
// 점수/게이지/최종화면 함수
// ---------------------------------------------------------------------------

void CatchMindGame::broadcastScoreDelta(const std::string &targetNodeId, int delta) {
    gameScores[targetNodeId] += delta;
    if (roleSock < 0) return;
    std::string payload = "CM|" + nodeId + "|SCORE_DELTA|" + targetNodeId + ":" + std::to_string(delta);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(37031);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    for (int i = 0; i < 3; ++i)
        sendto(roleSock, payload.c_str(), payload.size(), 0,
               reinterpret_cast<sockaddr *>(&to), sizeof(to));
    std::cout << "[점수] " << targetNodeId << " delta=" << delta
              << " total=" << gameScores[targetNodeId] << "\n";
}

void CatchMindGame::drawTimerGauge(int remainSec, int totalSec) {
    if (display == nullptr) return;
    const int gx = 4, gy = 54, gw = 138, gh = 5;
    display->drawRect(gx, gy, gw, gh, ui::CARD_ALT);
    int fillW = (remainSec * gw) / std::max(1, totalSec);
    fillW = std::max(0, std::min(gw, fillW));
    if (fillW > 0) {
        unsigned int color;
        if (remainSec > 30)      color = 0xFFD93D;  // 노랑
        else if (remainSec > 10) color = 0xFF8C1A;  // 주황
        else                     color = 0xFF3030;  // 빨강
        display->drawRect(gx, gy, fillW, gh, color);
    }
}

void CatchMindGame::showFinalScores() {
    if (display == nullptr) return;

    // 점수 내림차순 정렬
    std::vector<std::pair<int, std::string>> sorted;
    for (auto &kv : gameScores)
        sorted.push_back({kv.second, kv.first});
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });

    display->beginFrame();
    display->clearScreen(ui::BG_DARK);

    int cx = screenW / 2;
    drawPanelCard(display, cx - 200, 20, 400, 56, ui::NG, 0x3a0f0f, 0x2a0b0b);
    drawTextCentered(display, cx, 34, "GAME OVER", ui::NG, 4);
    drawTextCentered(display, cx, 62, "FINAL SCORES", ui::TEXT_DIM, 1);

    const char  *rankLabel[] = {"1ST", "2ND", "3RD"};
    const unsigned int rankColor[] = {0xFFD700, 0xC0C0C0, 0xCD7F32};

    int startY = 96;
    for (int i = 0; i < (int)sorted.size() && i < 3; ++i) {
        int y = startY + i * 62;
        unsigned int rc = rankColor[i];
        drawPanelCard(display, cx - 210, y, 420, 56, rc, ui::CARD, ui::BG_MID);
        display->drawText(cx - 200, y + 18, rankLabel[i], rc, 2);

        int playerNum = 0;
        const std::string &id = sorted[i].second;
        if (id.rfind("PLAYER", 0) == 0) {
            try {
                playerNum = std::stoi(id.substr(6));
            } catch (...) {
                playerNum = 0;
            }
        }

        std::string label;
        if (playerNum >= 1 && playerNum <= 3) {
            label = "Player " + std::to_string(playerNum);
        } else {
            label = id;
        }
        if (id == nodeId) {
            label += " (YOU)";
        }
        if (label.size() > 18) label = label.substr(label.size() - 18);
        display->drawText(cx - 140, y + 18, label.c_str(), ui::TEXT_MAIN, 2);
        std::string scoreStr = std::to_string(sorted[i].first) + " pts";
        drawTextCentered(display, cx + 150, y + 18, scoreStr.c_str(), rc, 2);
    }

    // 내 점수 강조 표시
    auto it = gameScores.find(nodeId);
    if (it != gameScores.end()) {
        std::string myStr = "YOUR SCORE: " + std::to_string(it->second) + " pts";
        drawTextCentered(display, cx, startY + 3 * 62 + 20, myStr.c_str(), ui::ACCENT, 2);
    }
    drawTextCentered(display, cx, screenH - 30, "TAP or press any key to exit", ui::TEXT_DIM, 1);
    display->endFrame();

    // 터치 버퍼 비우기
    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
    }

    // 최대 30초 대기 (터치 or 키)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        int maxfd = STDIN_FILENO;
        if (touchFd >= 0) { FD_SET(touchFd, &fds); if (touchFd > maxfd) maxfd = touchFd; }
        timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 200000;
        if (select(maxfd + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
        if (FD_ISSET(STDIN_FILENO, &fds)) break;
        if (touchFd >= 0 && FD_ISSET(touchFd, &fds)) {
            input_event ev{};
            bool tapped = false;
            while (read(touchFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 0) tapped = true;
            }
            if (tapped) break;
        }
    }
}
