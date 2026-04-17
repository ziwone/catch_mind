#include "game.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

namespace {

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
    nodeId = std::to_string(rd()) + "-" + std::to_string(getpid());

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

    display->clearScreen(Display::COLOR_BLACK);

    // 상단 캔버스
    // 출제자인 경우 금색(0xFFD700), 도전자인 경우 보라색(0x9932CC) 테두리
    unsigned int canvasBorderColor = isDrawerRole ? 0xFFD700 : 0x9932CC;
    display->drawRect(canvasX - 5, canvasY - 5, canvasW + 10, canvasH + 10, canvasBorderColor);
    display->drawRect(canvasX - 3, canvasY - 3, canvasW + 6, canvasH + 6, 0x333333);
    display->drawRect(canvasX, canvasY, canvasW, canvasH, Display::COLOR_BLACK);

    // 상단: 역할 표시 (DRAWER)
    if (isDrawerRole) {
        display->drawText(canvasX + 5, canvasY + 5, "DRAWER", 0xFFD700, 2);
    }

    // 하단 정답 영역 2분할
    int halfW = screenW / 2;
    display->drawRect(0, bottomY, screenW, panelH, 0x1a1a1a);

    // 도전자 구분 표시 및 색상 개선
    // 왼쪽: 플레이어1 (도전자)
    display->drawRect(1, bottomY + 1, halfW - 2, panelH - 2, 0x2a2a5a);
    display->drawRect(0, bottomY, halfW, panelH, 0x555577);
    display->drawText(5, bottomY + 5, "P1", Display::COLOR_WHITE, 2);

    // 오른쪽: 플레이어2 (도전자)
    display->drawRect(halfW + 1, bottomY + 1, halfW - 2, panelH - 2, 0x5a2a2a);
    display->drawRect(halfW, bottomY, screenW - halfW, panelH, 0x775555);
    display->drawText(screenW - halfW + 5, bottomY + 5, "P2", Display::COLOR_WHITE, 2);

    // 경계선
    display->drawRect(halfW - 2, bottomY, 4, panelH, 0x888888);
    display->drawRect(0, bottomY - 2, screenW, 4, 0x888888);

    drawStatus();
}

void CatchMindGame::drawStatus() {
    std::cout << "[round " << (round + 1) << "] "
              << "카테고리=" << currentCategory
              << ", 상태=" << (isDrawing ? "진행중" : "대기") << std::endl;
}

void CatchMindGame::start() {
    bgm.setVolume(50);
    bgm.play("/mnt/nfs/bgm/maple1.wav");

    std::cout << "=====================================\n";
    std::cout << "캐치마인드 멀티보드 프로토타입\n";
    std::cout << "- 같은 허브의 보드들이 역할을 자동 연동\n";
    std::cout << "- 출제자는 터치로 카테고리/주제어 선택\n";
    std::cout << "=====================================\n";

    initRoleSocket();

    while (true) {
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
        std::cout << "[시스템] 역할 선택 화면으로 복귀\n\n";
    }
}

void CatchMindGame::stop() {
    std::cout << "[시스템] 게임 종료\n";
    if (display != nullptr) {
        display->clearScreen(Display::COLOR_BLACK);
    }
}

bool CatchMindGame::roleSelection() {
    // 정답 후 라운드라면 (currentDrawerNodeId가 이미 설정됨) 자동 인정
    if (round > 0 && currentDrawerNodeId == nodeId) {
        std::cout << "[역할] 정답자 출제자 자동 인정\n";
        isDrawerRole = true;
        return true;
    }
    if (round > 0 && !isDrawerRole && !currentDrawerNodeId.empty()) {
        std::cout << "[역할] 이전 정답자가 출제자, 자동 도전자\n";
        return true;
    }

    if (display == nullptr) {
        return false;
    }

    display->clearScreen(Display::COLOR_BLACK);
    int midX = screenW / 2;
    int boxY = screenH / 3;
    int boxW = screenW / 3;
    int boxH = screenH / 3;
    int gap = 20;
    int drawerX = midX - boxW - gap;
    int challengerX = midX + gap;

    // 역할 버튼(텍스트 렌더링은 없어서 색상 박스로 표현)
    display->drawRect(drawerX - 4, boxY - 4, boxW + 8, boxH + 8, Display::COLOR_WHITE);
    display->drawRect(challengerX - 4, boxY - 4, boxW + 8, boxH + 8, Display::COLOR_WHITE);
    display->drawRect(drawerX, boxY, boxW, boxH, Display::COLOR_BLUE);      // left: drawer
    display->drawRect(challengerX, boxY, boxW, boxH, 0x505050);              // right: challenger
    display->drawText((screenW / 2) - 110, boxY - 60, "ROLE SELECT", Display::COLOR_WHITE, 3);
    display->drawText(drawerX + 24, boxY + (boxH / 2) - 12, "DRAWER", Display::COLOR_WHITE, 3);
    display->drawText(challengerX + 8, boxY + (boxH / 2) - 12, "CHALLENGER", Display::COLOR_WHITE, 2);

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
            if (receiveControlMessage(kind, value, senderIp, senderNodeId) && senderNodeId != nodeId && kind == "ROLE" &&
                value == "DRAWER") {
                isDrawerRole = false;
                drawerIp = senderIp;
                currentDrawerNodeId = senderNodeId;
                std::cout << "[역할] " << drawerIp << " 보드가 출제자 선택 -> 자동 도전자 전환\n";
                // 바로 return해서 runChallengerStandby()로 진입 (블로킹하면 DRAWING_START 놓칠 수 있음)
                return true;
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
                            showTransitionScreen("CHALLENGER", "PLEASE WAIT...", 1500);
                            usleep(300000);
                            std::cout << "[역할] 도전자 선택 완료\n";
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
                showTransitionScreen("CHALLENGER", "PLEASE WAIT...", 1500);
                std::cout << "[역할] 도전자 선택 완료\n";
                return true;
            }

            std::cout << "[역할] 올바른 입력: 1, 2, q\n";
        }
    }
}

void CatchMindGame::runChallengerStandby() {
    isDrawing = false;
    if (display != nullptr) {
        display->clearScreen(Display::COLOR_BLACK);
        display->drawText((screenW / 2) - 150, (screenH / 2) - 30, "WORD SELECTING...", Display::COLOR_WHITE, 3);
        display->drawText((screenW / 2) - 110, (screenH / 2) + 20, "PLEASE WAIT", 0xaaaaaa, 2);
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
                        display->clearScreen(Display::COLOR_BLACK);
                        display->drawText((screenW / 2) - 150,
                                          (screenH / 2) - 30,
                                          "WORD SELECTING...",
                                          Display::COLOR_WHITE,
                                          3);
                        display->drawText((screenW / 2) - 110, (screenH / 2) + 20, "PLEASE WAIT", 0xaaaaaa, 2);
                    } else if (value == "DRAWING_START") {
                        std::cout << "[도전자] DRAWING_START 수신! 출제자=" << senderIp << "\n";
                        currentDrawerNodeId = senderNodeId;
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

    drawGameLayout();
    resetCanvas();
    paintAnswerPanel(1, 0x303030);
    paintAnswerPanel(2, 0x303030);
    display->drawText(28, 24, "CHALLENGER VIEW", Display::COLOR_WHITE, 2);

    std::cout << "[도전자] 실시간 그림 수신 시작\n";

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
        tv.tv_usec = 10000;

        int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("select(challenger-live)");
            break;
        }

        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind;
            std::string value;
            std::string senderIp;
            std::string senderNodeId;
            while (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId == nodeId) {
                    continue;
                }

                if (!currentDrawerNodeId.empty() && senderNodeId != currentDrawerNodeId) {
                    continue;
                }

                if (kind == "DRAW") {
                    std::stringstream parse(value);
                    std::string sxStr;
                    std::string syStr;
                    std::string colorStr;
                    if (std::getline(parse, sxStr, ',') && std::getline(parse, syStr, ',') &&
                        std::getline(parse, colorStr, ',')) {
                        try {
                            int sx = std::stoi(sxStr);
                            int sy = std::stoi(syStr);
                            unsigned int color = (unsigned int)std::stoul(colorStr, nullptr, 16);
                            if (sx >= canvasX && sx < canvasX + canvasW && sy >= canvasY && sy < canvasY + canvasH) {
                                display->drawRect(sx - 2, sy - 2, 5, 5, color);
                            }
                        } catch (...) {
                        }
                    }
                } else if (kind == "CLEAR") {
                    resetCanvas();
                } else if (kind == "STATUS" && value == "ROUND_END") {
                    std::cout << "[도전자] 라운드 종료\n";
                    return;
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                break;
            }
            if (line == "q") {
                std::cout << "[도전자] 강제 종료\n";
                break;
            }
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

    display->clearScreen(Display::COLOR_BLACK);

    int cx = screenW / 2;
    int cy = screenH / 2;

    // 중앙 박스
    int bw = screenW * 3 / 4;
    int bh = screenH / 3;
    int bx = cx - bw / 2;
    int by = cy - bh / 2;
    display->drawRect(bx - 4, by - 4, bw + 8, bh + 8, Display::COLOR_YELLOW);
    display->drawRect(bx, by, bw, bh, 0x1a1a1a);

    // 선택된 텍스트 표시
    std::string label = toDisplayLabel(selectedText);
    int textW = (int)label.size() * 6 * 3;
    display->drawText(cx - textW / 2, by + 20, label, Display::COLOR_WHITE, 3);

    // "CONFIRM?" 텍스트
    display->drawText(cx - 70, by + 70, "CONFIRM?", Display::COLOR_YELLOW, 2);

    // 좌측: YES 버튼 (좌클릭 또는 1)
    int btnW = (screenW - 40) / 2;
    int btnH = 50;
    int leftBtnX = 20;
    int rightBtnX = 20 + btnW + 20;
    int btnY = screenH - 100;

    display->drawRect(leftBtnX - 2, btnY - 2, btnW + 4, btnH + 4, Display::COLOR_GREEN);
    display->drawRect(leftBtnX, btnY, btnW, btnH, 0x0a5a0a);
    display->drawText(leftBtnX + 20, btnY + 15, "YES(1)", Display::COLOR_GREEN, 2);

    display->drawRect(rightBtnX - 2, btnY - 2, btnW + 4, btnH + 4, Display::COLOR_RED);
    display->drawRect(rightBtnX, btnY, btnW, btnH, 0x5a0a0a);
    display->drawText(rightBtnX + 15, btnY + 15, "NO(2)", Display::COLOR_RED, 2);

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
    std::cout << "\n[명령어]\n";
    std::cout << "  touch drag      : 상단 캔버스에 그리기\n";
    std::cout << "  p               : 펜 on/off\n";
    std::cout << "  c               : 캔버스 초기화\n";
    std::cout << "  1 2 3 4 5       : 색상(흰/빨/초/파/노)\n";
    std::cout << "  guess <word>    : 참가자1 정답 제출\n";
    std::cout << "  guess2 <word>   : 참가자2 정답 제출\n";
    std::cout << "  q               : 라운드 종료\n\n";
}

void CatchMindGame::runSingleBoardRound() {
    isDrawing = true;
    penDown = true;
    brushColor = Display::COLOR_WHITE;

    drawGameLayout();
    resetCanvas();

    player1LatestAnswer.clear();
    player2LatestAnswer.clear();

    paintAnswerPanel(1, 0x303030);
    paintAnswerPanel(2, 0x303030);

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

    std::string line;
    while (isDrawing) {
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

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 10000;

        int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            std::perror("select");
            break;
        }

        if (touchFd >= 0 && FD_ISSET(touchFd, &readfds)) {
            processTouchEvents();
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (!std::getline(std::cin, line)) {
                isDrawing = false;
                break;
            }

            if (line == "q") {
                std::cout << "[라운드] 출제자가 종료\n";
                isDrawing = false;
                break;
            }

            if (line.rfind("guess2 ", 0) == 0) {
                if (handleGuess(2, line.substr(7))) {
                    isDrawing = false;
                }
                continue;
            }

            if (line.rfind("guess ", 0) == 0) {
                if (handleGuess(1, line.substr(6))) {
                    isDrawing = false;
                }
                continue;
            }

            handleDrawCommand(line);
        }
    }

    broadcastStatusMessage("ROUND_END");

    sleep(1);
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
        brushColor = Display::COLOR_WHITE;
        return true;
    }
    if (cmd == "2") {
        brushColor = Display::COLOR_RED;
        return true;
    }
    if (cmd == "3") {
        brushColor = Display::COLOR_GREEN;
        return true;
    }
    if (cmd == "4") {
        brushColor = Display::COLOR_BLUE;
        return true;
    }
    if (cmd == "5") {
        brushColor = Display::COLOR_YELLOW;
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

    display->drawRect(x + 4, bottomY + 4, w - 8, panelH - 8, color);
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

void CatchMindGame::processTouchEvents() {
    if (touchFd < 0 || display == nullptr || !isDrawing) {
        return;
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
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            touchPressed = (ev.value != 0);
            if (!touchPressed) {
                strokeActive = false;
            }
        } else if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) {
            touchPressed = (ev.value >= 0);
            if (!touchPressed) {
                strokeActive = false;
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
                int steps = std::max(std::abs(dx), std::abs(dy)) / 2;
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

void CatchMindGame::closeRoleSocket() {
    if (roleSock >= 0) {
        close(roleSock);
        roleSock = -1;
    }
}

void CatchMindGame::showTransitionScreen(const std::string &line1, const std::string &line2, int durationMs) {
    if (display == nullptr) {
        usleep(durationMs * 1000);
        return;
    }

    display->clearScreen(Display::COLOR_BLACK);

    int cx = screenW / 2;
    int cy = screenH / 2;

    // 중앙 테두리 박스
    int bw = screenW * 2 / 3;
    int bh = screenH / 4;
    int bx = cx - bw / 2;
    int by = cy - bh / 2;
    display->drawRect(bx - 4, by - 4, bw + 8, bh + 8, Display::COLOR_WHITE);
    display->drawRect(bx, by, bw, bh, 0x1a1a2e);

    // 첫 번째 줄 (큰 글씨)
    int textW1 = (int)line1.size() * 6 * 3;
    display->drawText(cx - textW1 / 2, by + bh / 4 - 10, line1, Display::COLOR_WHITE, 3);

    // 두 번째 줄 (작은 글씨)
    int textW2 = (int)line2.size() * 6 * 2;
    display->drawText(cx - textW2 / 2, by + bh * 3 / 4 - 7, line2, 0xaaaaaa, 2);

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

    std::stringstream ss;
    ss << x << "," << y << "," << std::hex << color;
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
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            bool wasPressed = touchPressed;
            touchPressed = (ev.value != 0);
            if (wasPressed && !touchPressed && touchHasX && touchHasY) {
                return mapTouchToScreen(touchRawX, touchRawY, sx, sy);
            }
        } else if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) {
            bool wasPressed = touchPressed;
            touchPressed = (ev.value >= 0);
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

    display->clearScreen(Display::COLOR_BLACK);
    display->drawText(30, 24, title + " SELECT", Display::COLOR_WHITE, 3);

    const int menuTop = 90;
    const int menuBottom = screenH - 30;
    const int itemGap = 12;
    int itemH = (menuBottom - menuTop - ((int)options.size() - 1) * itemGap) / (int)options.size();
    itemH = std::max(40, itemH);

    for (size_t i = 0; i < options.size(); ++i) {
        int y = menuTop + (int)i * (itemH + itemGap);
        display->drawRect(30, y, screenW - 60, itemH, 0x202020);
        display->drawRect(34, y + 4, screenW - 68, itemH - 8, highlightColor);

        std::string line = std::to_string((int)i + 1) + ". " + toDisplayLabel(options[i]);
        display->drawText(50, y + std::max(8, (itemH / 2) - 10), line, Display::COLOR_WHITE, 2);

        std::cout << "  " << (i + 1) << ") " << options[i] << " [" << toDisplayLabel(options[i]) << "]\n";
    }

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
