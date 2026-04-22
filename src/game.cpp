#include "game.h"
#include "wordbank.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/gpio.h>
#include <iostream>
#include <linux/input.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

namespace {

namespace ui {

constexpr unsigned int BG_DARK    = 0x041810;
constexpr unsigned int BG_MID     = 0x07251a;
constexpr unsigned int CARD       = 0x0b2e20;
constexpr unsigned int CARD_ALT   = 0x0e3a2a;
constexpr unsigned int STROKE     = 0x1a7a55;
constexpr unsigned int ACCENT     = 0x00ffd5;
constexpr unsigned int ACCENT_WARM= 0xff5ea8;
constexpr unsigned int TEXT_MAIN  = 0xf0fff8;
constexpr unsigned int TEXT_DIM   = 0x7ecfaa;
constexpr unsigned int P1_ACCENT  = 0x32c8ff;
constexpr unsigned int P2_ACCENT  = 0xff5ea8;
constexpr unsigned int OK         = 0x00e87a;
constexpr unsigned int NG         = 0xff4d7a;
}
constexpr int TIMER_BAR_H = 28;  // full-width timer bar height

// ── Hardware button (GPIO character device, /dev/gpiochipN) ──────────────
// Linux GPIO cdev API (kernel >= 4.8). Returns an event fd polled with POLLIN.
// chip=0 tries /dev/gpiochip0 first, then /dev/gpiochip1 on failure.
static int gpioOpenCdev(int line) {
    for (int chip = 0; chip <= 1; ++chip) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/gpiochip%d", chip);
        int chipFd = open(path, O_RDONLY);
        if (chipFd < 0) continue;

        struct gpioevent_request req = {};
        req.lineoffset  = (uint32_t)line;
        req.handleflags = GPIOHANDLE_REQUEST_INPUT;
        req.eventflags  = GPIOEVENT_REQUEST_FALLING_EDGE;
        snprintf(req.consumer_label, sizeof(req.consumer_label), "catch_mind");

        if (ioctl(chipFd, GPIO_GET_LINEEVENT_IOCTL, &req) < 0) {
            fprintf(stderr, "[Button] gpiochip%d line%d: %s\n", chip, line, strerror(errno));
            close(chipFd);
            continue;
        }
        close(chipFd);
        fprintf(stderr, "[Button] gpiochip%d line%d fd=%d ready\n", chip, line, req.fd);
        return req.fd;
    }
    fprintf(stderr, "[Button] line%d: all chips failed\n", line);
    return -1;
}

// Watch one GPIO event fd with poll(POLLIN); set flag on falling edge.
// Runs in a background thread. Exits when *running becomes false.
static void gpioWatchThread(int gpioFd, std::atomic<bool> *pressed,
                            const std::atomic<bool> *running) {
    if (gpioFd < 0) return;
    struct pollfd pfd { gpioFd, POLLIN, 0 };
    while (running->load()) {
        pfd.revents = 0;
        if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN)) {
            struct gpioevent_data ev;
            read(gpioFd, &ev, sizeof(ev));
            pressed->store(true);
            fprintf(stderr, "[Button] fd=%d pressed (id=%u)\n", gpioFd, ev.id);
        }
    }
}



static void drawBg(Display *display, int screenW, int screenH, int style) {
    if (display == nullptr) return;


    unsigned int topColor, midColor, botColor;
    if (style == 0) {
        // Menu: deep green gradient
        topColor = 0x041c14; midColor = 0x073d28; botColor = 0x041810;
    } else if (style == 1) {

        topColor = 0x041220; midColor = 0x0a2840; botColor = 0x041018;
    } else if (style == 2) {

        topColor = 0x200818; midColor = 0x3a0f28; botColor = 0x180510;
    } else {

        topColor = 0x050f0a; midColor = 0x0a1e14; botColor = 0x050f0a;
    }

    int h3 = screenH / 3;

    for (int y = 0; y < h3; ++y) {
        unsigned int r = ((topColor>>16)&0xff) + (int)(((midColor>>16)&0xff) - (int)((topColor>>16)&0xff)) * y / h3;
        unsigned int g = ((topColor>>8)&0xff)  + (int)(((midColor>>8)&0xff)  - (int)((topColor>>8)&0xff))  * y / h3;
        unsigned int b = (topColor&0xff)        + (int)((midColor&0xff)        - (int)(topColor&0xff))        * y / h3;
        display->drawRect(0, y, screenW, 1, (r<<16)|(g<<8)|b);
    }

    for (int y = h3; y < screenH; ++y) {
        int t = y - h3;
        int span = screenH - h3;
        unsigned int r = ((midColor>>16)&0xff) + (int)(((botColor>>16)&0xff) - (int)((midColor>>16)&0xff)) * t / span;
        unsigned int g = ((midColor>>8)&0xff)  + (int)(((botColor>>8)&0xff)  - (int)((midColor>>8)&0xff))  * t / span;
        unsigned int b = (midColor&0xff)        + (int)((botColor&0xff)        - (int)(midColor&0xff))        * t / span;
        display->drawRect(0, y, screenW, 1, (r<<16)|(g<<8)|b);
    }


    unsigned int dotColor = (style == 2) ? 0xff4d7a : (style == 1) ? 0x32c8ff : 0x00e87a;
    const int bx[] = {60,200,400,650,880,130,500,760,40,300,570,820};
    const int by[] = {20,50,15,40,25,100,80,55,140,120,150,100};
    for (int i = 0; i < 12; ++i) {
        int sz = (i % 3 == 0) ? 6 : (i % 3 == 1) ? 4 : 3;
        display->drawRect(bx[i], by[i], sz, sz, dotColor);
        display->drawRect(bx[i]+1, by[i]-1, sz-2, 1, 0xffffff);
    }
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
    // Words are now stored as plain English uppercase — return as-is.
    return text;
}

}

CatchMindGame::CatchMindGame() {
    std::random_device rd;
    rng.seed(rd());
    myLocalIp = getLocalIpAddress();
    int mappedPlayer = getPlayerNumberFromIp(myLocalIp);
    myBoardNum = (mappedPlayer >= 1 && mappedPlayer <= 3) ? mappedPlayer : 1;
    if (mappedPlayer >= 1 && mappedPlayer <= 3) {
        nodeId = "PLAYER" + std::to_string(mappedPlayer);
    } else {
        nodeId = std::to_string(rd()) + "-" + std::to_string(getpid());
    }

    loadWordBank(wordBank);
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




    const int infoPanelW = 150;
    const int padding = 8;

    int topRatioPct = 65;
    topH = (screenH * topRatioPct) / 100;
    topH = std::max(140, std::min(screenH - 90, topH));
    bottomY = topH + TIMER_BAR_H;
    panelH = screenH - bottomY;


    canvasX = infoPanelW + padding;
    canvasY = padding;
    canvasW = std::max(100, screenW - infoPanelW - 2*padding);
    canvasH = std::max(100, topH - 2*padding);


    display->drawRect(0, 0, infoPanelW, topH, ui::BG_MID);
    drawPanelCard(display, 2, 2, infoPanelW - 4, topH - 4, ui::STROKE, ui::CARD, 0x0b151f);

    display->drawRect(4, 4, infoPanelW - 8, 20, ui::STROKE);
    display->drawText(8, 8, "INFO", ui::TEXT_DIM, 1);

    std::string brdLabel = "BRD" + std::to_string(myBoardNum);
    display->drawText(infoPanelW - 32, 8, brdLabel.c_str(), ui::ACCENT, 1);

    // 캐릭터를 배경 직후 먼저 그림 (endFrame 직전에 그리면 순간 빈 화면에만 캐릭터 보임)
    {
        int charSz = std::min(80, topH - 50);
        if (charSz >= 20) {
            int cix = (infoPanelW - charSz) / 2;
            int ciy = topH - charSz - 4;
            std::string cpath = "/mnt/nfs/img/player" + std::to_string(myBoardNum) + "/normal.ppm";
            if (!display->drawPNG(cpath, cix, ciy, charSz, charSz))
                display->drawPNG("/mnt/nfs/img/character.ppm", cix, ciy, charSz, charSz);
        }
    }

    unsigned int canvasAccent = isDrawerRole ? ui::ACCENT_WARM : ui::ACCENT;
    drawPanelCard(display, canvasX - 8, canvasY - 8, canvasW + 16, canvasH + 16,
                  canvasAccent, ui::CARD, 0x0b141c);
    display->drawRect(canvasX, canvasY, canvasW, canvasH, Display::COLOR_BLACK);

    if (isDrawerRole) {
        display->drawText(canvasX + 14, canvasY + 14, "DRAWER", 0xFFE000, 1);
    } else {
        display->drawText(canvasX + 14, canvasY + 14, "CHALLENGER", 0xFFE000, 1);
    }


    if (cursorX < canvasX || cursorX >= canvasX + canvasW ||
        cursorY < canvasY || cursorY >= canvasY + canvasH) {
        cursorX = canvasX + (canvasW / 2);
        cursorY = canvasY + (canvasH / 2);
    }


    int halfW = screenW / 2;
    display->drawRect(0, bottomY, screenW, panelH, ui::BG_MID);
    drawPanelCard(display, 0, bottomY, halfW, panelH, ui::P1_ACCENT, ui::CARD_ALT, 0x102336);
    drawPanelCard(display, halfW, bottomY, screenW - halfW, panelH, ui::P2_ACCENT, ui::CARD_ALT, 0x2a1b21);


    display->drawRect(0, topH, screenW, TIMER_BAR_H, 0x0a1810);
    display->drawRect(halfW - 1, bottomY, 2, panelH, ui::STROKE);

    display->drawText(10, bottomY + 8, "P1", ui::TEXT_MAIN, 2);
    display->drawText(halfW + 10, bottomY + 8, "P2", ui::TEXT_MAIN, 2);

    drawStatus();
    display->endFrame();
}

void CatchMindGame::drawStatus() {
    std::cout << "[round " << (round + 1) << "] "
              << "" << currentCategory
              << "" << (isDrawing ? "" : "") << std::endl;
}

void CatchMindGame::start() {

    printf("\033[?25l");
    fflush(stdout);

    std::cout << "=====================================\n";
    std::cout << "=====================================\n";

    initRoleSocket();

    while (true) {  // restart loop: returns here after each game ends

        // ── Reset all game state ──────────────────────────────────────────
        gameScores.clear();
        gameScores["PLAYER1"] = 0;
        gameScores["PLAYER2"] = 0;
        gameScores["PLAYER3"] = 0;
        round = 0;
        isDrawerRole = false;
        drawerIp.clear();
        currentDrawerNodeId.clear();
        myPlayerNumber = 0;
        myAnswerInput.clear();
        receivedAnswer1.clear();
        receivedAnswer2.clear();
        answerReceived1 = false;
        answerReceived2 = false;

        if (!waitForAllPlayersReadyAtStart()) {
            stop();
            return;
        }

        bgm.setVolume(80);
        bgm.play("/mnt/nfs/bgm/maple1.wav");

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
            std::cout << "[]     ( " << round << "/" << MAX_ROUNDS << ")\n\n";
        }

        broadcastStatusMessage("GAME_OVER");
        showFinalScores();
        // showFinalScores() returns after the player confirms → restart
        std::cout << "[Game] Restarting...\n";
    }
}

void CatchMindGame::stop() {
    printf("\033[?25h");
    fflush(stdout);

    std::cout << "[]  \n";
    if (display != nullptr) {
        display->beginFrame();
        display->clearScreen(Display::COLOR_BLACK);
        display->endFrame();
    }
}

bool CatchMindGame::roleSelection() {

    if (round > 0 && isDrawerRole && currentDrawerNodeId == nodeId) {
        std::cout << "[]    \n";
        broadcastDrawerSelected();
        return true;
    }
    if (round > 0 && !isDrawerRole) {
        std::cout << "[]   ,  \n";
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
    drawTextCentered(display, drawerX + (boxW / 2), boxY + (boxH / 2) - 14, "DRAWER", 0xFFE000, 3);
    drawTextCentered(display, challengerX + (boxW / 2), boxY + (boxH / 2) - 10, "CHALLENGER", 0xFFE000, 2);
    display->endFrame();

    std::cout << "[]  =,  =\n";
    std::cout << "[]  : 1(), 2(), q()\n";
    std::cout << "[Role] Drawer selected by other board\n";


    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX   = false;
        touchHasY   = false;
    }

    std::set<std::string> joinedChallengerNodes;  // 도전자 선택한 보드 추적

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
                if (kind == "STATUS" && value == "GAME_OVER") {
                    std::cout << "[Challenger] GAME_OVER -> final scores\n";
                    showFinalScores();
                    return false;
                }
                if (kind == "ROLE" && value == "DRAWER") {
                    isDrawerRole = false;
                    drawerIp = senderIp;
                    currentDrawerNodeId = senderNodeId;
                    if (myLocalIp.empty()) myLocalIp = getLocalIpAddress();
                    int myBoardNum = getPlayerNumberFromIp(myLocalIp);
                    int drawerBoardNum = getPlayerNumberFromIp(drawerIp);
                    myPlayerNumber = getChallengerSlotByDrawer(myBoardNum, drawerBoardNum);
                    if (myPlayerNumber == 0) myPlayerNumber = 1;
                    std::cout << "[] drawer=" << drawerIp
                              << " myIp=" << myLocalIp
                              << " => challenger P" << myPlayerNumber << "\n";
                    std::cout << "[Role] Drawer selected by other board\n";
                    return true;
                }
                // 도전자 선택 카운트: 2명이 도전자를 골랐으면 자동으로 출제자
                if (kind == "STATUS" && value == "CHALLENGER_JOIN") {
                    joinedChallengerNodes.insert(senderNodeId);
                    std::cout << "[Role] CHALLENGER_JOIN from " << senderNodeId
                              << " (" << joinedChallengerNodes.size() << "/2)\n";
                    if ((int)joinedChallengerNodes.size() >= 2) {
                        std::cout << "[Role] 2 challengers joined -> auto DRAWER\n";
                        isDrawerRole = true;
                        drawerIp.clear();
                        currentDrawerNodeId = nodeId;
                        broadcastDrawerSelected();
                        showTransitionScreen("AUTO DRAWER", "ALL CHOSE CHALLENGER", 1500);
                        return true;
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
                } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                    bool wasPressed = touchPressed;
                    touchPressed = (ev.value != 0);


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
                            // ①
                            broadcastDrawerSelected();
                            // ②
                            showTransitionScreen("DRAWER SELECTED", "CHOOSE CATEGORY", 1500);
                            std::cout << "[]   \n";
                            return true;
                        }

                        if (onChallenger) {
                            isDrawerRole = false;
                            drawerIp.clear();
                            currentDrawerNodeId.clear();
                            

                            myLocalIp = getLocalIpAddress();
                            std::cout << "[]  IP: " << myLocalIp << "\n";
                            
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
                                            std::cout << "[] P" << myNum << " vs P" << otherNum 
                                                      << " ->  P" << myPlayerNumber << "\n";
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
                            std::cout << "[] P" << myPlayerNumber << "  \n";
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
                std::cout << "[]   \n";
                return true;
            }

            if (input == "2") {
                isDrawerRole = false;
                drawerIp.clear();
                currentDrawerNodeId.clear();
                

                myLocalIp = getLocalIpAddress();
                std::cout << "[]  IP: " << myLocalIp << "\n";
                
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
                                std::cout << "[] P" << myNum << " vs P" << otherNum 
                                          << " ->  P" << myPlayerNumber << "\n";
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
                std::cout << "[] P" << myPlayerNumber << "  \n";
                return true;
            }

            std::cout << "[]  : 1, 2, q\n";
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

    std::cout << "[] =" << (drawerIp.empty() ? "  " : drawerIp) << "\n";
    std::cout << "[]    .\n";

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
                    if (value == "GAME_OVER") {
                        std::cout << "[Challenger] GAME_OVER -> final scores\n";
                        showFinalScores();
                        return;
                    }
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
                    } else if (value == "GAME_READY" && !drawerIp.empty()) {

                        std::cout << "[] GAME_READY  (DRAWING_START ) ->   \n";
                        if (myLocalIp.empty()) myLocalIp = getLocalIpAddress();
                        int myBN = getPlayerNumberFromIp(myLocalIp);
                        int drBN = getPlayerNumberFromIp(drawerIp);
                        int slot = getChallengerSlotByDrawer(myBN, drBN);
                        if (slot != 0) myPlayerNumber = slot;
                        if (myPlayerNumber == 0) myPlayerNumber = 1;
                        runChallengerLiveRound();
                        return;
                    } else if (value == "DRAWING_START") {
                        std::cout << "[] DRAWING_START ! =" << senderIp << "\n";
                        drawerIp = senderIp;
                        currentDrawerNodeId = senderNodeId;


                        if (myLocalIp.empty()) myLocalIp = getLocalIpAddress();
                        int myBoardNum = getPlayerNumberFromIp(myLocalIp);
                        int drawerBoardNum = getPlayerNumberFromIp(drawerIp);
                        int slot = getChallengerSlotByDrawer(myBoardNum, drawerBoardNum);
                        if (slot != 0) myPlayerNumber = slot;
                        if (myPlayerNumber == 0) myPlayerNumber = 1;
                        std::cout << "[]  : " << myBoardNum
                                  << ", " << drawerBoardNum
                                  << " => P" << myPlayerNumber << "\n";


                        showTransitionScreen("GAME START", "GET READY!", 1500);

                        waitForGameReady(4000);
                        std::cout << "[]    \n";
                        runChallengerLiveRound();
                        std::cout << "[] runChallengerLiveRound \n";
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


    if (myPlayerNumber != 1 && myPlayerNumber != 2) {
        if (myLocalIp.empty()) myLocalIp = getLocalIpAddress();
        int myBoardNum = getPlayerNumberFromIp(myLocalIp);
        int drawerBoardNum = getPlayerNumberFromIp(drawerIp);
        int slot = getChallengerSlotByDrawer(myBoardNum, drawerBoardNum);
        myPlayerNumber = (slot == 0) ? 1 : slot;
        std::cout << "[] P : myIp=" << myLocalIp
                  << ", drawerIp=" << drawerIp
                  << " => P" << myPlayerNumber << "\n";
    }


    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX = false;
        touchHasY = false;
    }


    isDrawing = false;
    drawGameLayout();

    // Round timer
    auto challengerRoundStart = std::chrono::steady_clock::now();
    const int ROUND_TIMEOUT_SEC = 60;
    int challengerLastSec = -1;
    std::string challengerMood = "normal";
    auto moodCryUntil = std::chrono::steady_clock::now();
    std::string hintWord;  // populated when HINT# is received

    const int halfW = screenW / 2;
    const int myPanelX = (myPlayerNumber == 1) ? 0 : halfW;
    const int myPanelW = (myPlayerNumber == 1) ? halfW : (screenW - halfW);
    const int btnW = 130;
    const int btnH = 48;
    const int btnGap = 12;
    const int btnY = bottomY + panelH - btnH - 8;
    const int clearW = btnW;
    const int clearH = btnH;
    const int clearX = myPanelX + (myPanelW - (btnW * 2 + btnGap)) / 2;
    const int clearY = btnY;
    const int btnX = clearX + clearW + btnGap;
    const int writeX = myPanelX + 4;
    const int writeY = bottomY + 4;
    const int writeW = myPanelW - 8;
    const int writeH = std::max(40, btnY - writeY - 4);
    const int writeMax = 999;

    bool answerInkWritten = false;
    bool answerStrokeActive = false;
    int answerLastX = 0;
    int answerLastY = 0;
    bool haveRecentAnswerEnd = false;
    int recentAnswerEndX = 0;
    int recentAnswerEndY = 0;
    auto recentAnswerEndTime = std::chrono::steady_clock::now();
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

        queuedInkPoints.push_back({-1, -1});
    };

    auto lastTapTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(300);
    int lastTapX = -10000;
    int lastTapY = -10000;

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

    auto saveRecentAnswerEnd = [&](int sx, int sy) {
        recentAnswerEndX = sx;
        recentAnswerEndY = sy;
        recentAnswerEndTime = std::chrono::steady_clock::now();
        haveRecentAnswerEnd = true;
    };

    auto recentAnswerAgeMs = [&]() -> long long {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - recentAnswerEndTime).count();
    };


    bool otherStrokeActive = false;
    int otherLastX = 0, otherLastY = 0;

    auto drawOtherAnswerPoint = [&](int slot, int nx, int ny) {
        int hw = screenW / 2;
        int panelX = (slot == 1) ? 0 : hw;
        int panelW = (slot == 1) ? hw : (screenW - hw);
        int areaX = panelX + 8;
        int areaY = bottomY + 4;
        int areaW = std::max(10, panelW - 16);
        int areaH = std::max(10, panelH - 4 - 48 - 8 - 4);
        int sx = areaX + (nx * std::max(1, areaW - 1)) / 999;
        int sy = areaY + (ny * std::max(1, areaH - 1)) / 999;
        if (!otherStrokeActive) {
            display->drawRect(sx - 1, sy - 1, 2, 2, Display::COLOR_WHITE);
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
            display->drawRect(px - 1, py - 1, 2, 2, Display::COLOR_WHITE);
        }
        otherLastX = sx;
        otherLastY = sy;
    };

    auto clearAnswerAreaForSlot = [&](int slot) {
        int hw = screenW / 2;
        int panelX = (slot == 1) ? 0 : hw;
        int panelW = (slot == 1) ? hw : (screenW - hw);
        int areaX = panelX + 8;
        int areaY = bottomY + 4;
        int areaW = std::max(10, panelW - 16);
        int areaH = std::max(10, panelH - 4 - 48 - 8 - 4);
        drawPanelCard(display, areaX, areaY, areaW, areaH, ui::STROKE, ui::CARD, 0x0b151f);
        if (slot == myPlayerNumber) {
            display->drawText(areaX + 6, areaY + 6, "TOUCH WRITE", ui::TEXT_DIM, 1);
        }
    };

    auto redrawPanels = [&]() {
        paintAnswerPanel(1, 0x102336);
        paintAnswerPanel(2, 0x2a1b21);

        {
            int myPx = (myPlayerNumber == 1) ? 0 : halfW;
            int myPw = (myPlayerNumber == 1) ? halfW : (screenW - halfW);
            for (int t = 0; t < 2; ++t) {
                display->drawRect(myPx - t,         bottomY - t,     myPw + t*2, 1,           ui::ACCENT_WARM);
                display->drawRect(myPx - t,         bottomY - t,     1,          panelH + t*2, ui::ACCENT_WARM);
                display->drawRect(myPx - t,         bottomY + panelH + t - 1, myPw + t*2, 1,  ui::ACCENT_WARM);
                display->drawRect(myPx + myPw + t - 1, bottomY - t,  1,          panelH + t*2, ui::ACCENT_WARM);
            }
        }

        if (myPlayerNumber == 1) {
            display->drawText(halfW + 8, bottomY + 6,
                              receivedAnswer2.empty() ? "P2: waiting..." : ("P2: " + receivedAnswer2).substr(0, 16),
                              ui::TEXT_DIM, 1);
        } else {
            display->drawText(8, bottomY + 6,
                              receivedAnswer1.empty() ? "P1: waiting..." : ("P1: " + receivedAnswer1).substr(0, 16),
                              ui::TEXT_DIM, 1);
        }

        drawPanelCard(display, writeX, writeY, writeW, writeH, ui::STROKE, ui::CARD, 0x0b151f);
        display->drawText(writeX + 6, writeY + 6, "TOUCH WRITE", ui::TEXT_DIM, 1);

        drawPanelCard(display, clearX, clearY, clearW, clearH, ui::ACCENT_WARM, 0x533611, 0x3c280f);
        drawTextCentered(display, clearX + clearW / 2, clearY + btnH / 2 - 8, "CLEAR", ui::ACCENT_WARM, 1);

        unsigned int submitColor = submitLocked ? 0x3a3f44 : (answerInkWritten ? 0x1f5c3b : 0x30483a);
        unsigned int submitEdge = submitLocked ? ui::TEXT_DIM : ui::OK;
        drawPanelCard(display, btnX, btnY, btnW, btnH, submitEdge, submitColor, submitColor);
        drawTextCentered(display, btnX + btnW / 2, btnY + btnH / 2 - 8, "SUBMIT", submitEdge, 1);
    };

    auto redrawSubmitOnly = [&]() {
        unsigned int submitColor = submitLocked ? 0x3a3f44 : (answerInkWritten ? 0x1f5c3b : 0x30483a);
        unsigned int submitEdge = submitLocked ? ui::TEXT_DIM : ui::OK;
        drawPanelCard(display, btnX, btnY, btnW, btnH, submitEdge, submitColor, submitColor);
        drawTextCentered(display, btnX + btnW / 2, btnY + btnH / 2 - 8, "SUBMIT", submitEdge, 1);
    };


    auto redrawMyPanelClean = [&]() {
        unsigned int ownBg = (myPlayerNumber == 1) ? 0x102336u : 0x2a1b21u;
        paintAnswerPanel(myPlayerNumber, ownBg);

        int myPx = (myPlayerNumber == 1) ? 0 : halfW;
        int myPw = (myPlayerNumber == 1) ? halfW : (screenW - halfW);
        for (int t = 0; t < 2; ++t) {
            display->drawRect(myPx - t,         bottomY - t,     myPw + t*2, 1,           ui::ACCENT_WARM);
            display->drawRect(myPx - t,         bottomY - t,     1,          panelH + t*2, ui::ACCENT_WARM);
            display->drawRect(myPx - t,         bottomY + panelH + t - 1, myPw + t*2, 1,  ui::ACCENT_WARM);
            display->drawRect(myPx + myPw + t - 1, bottomY - t,  1,          panelH + t*2, ui::ACCENT_WARM);
        }

        drawPanelCard(display, writeX, writeY, writeW, writeH, ui::STROKE, ui::CARD, 0x0b151f);
        display->drawText(writeX + 6, writeY + 6, "TOUCH WRITE", ui::TEXT_DIM, 1);

        drawPanelCard(display, clearX, clearY, clearW, clearH, ui::ACCENT_WARM, 0x533611, 0x3c280f);
        drawTextCentered(display, clearX + clearW / 2, clearY + btnH / 2 - 8, "CLEAR", ui::ACCENT_WARM, 1);

        redrawSubmitOnly();
    };

    std::cout << "[P" << myPlayerNumber << "]   \n";
    std::cout << "        submit / : submit, q\n";
    display->beginFrame();
    redrawPanels();
    display->endFrame();

    // ── GPIO buttons for challenger (SW2=CLEAR, SW3=SUBMIT) ───────────────
    struct ChlGpioScope {
        std::atomic<bool> running{true};
        std::atomic<bool> sw2{false};  // CLEAR
        std::atomic<bool> sw3{false};  // SUBMIT
        std::thread t2, t3;
        int fd2 = -1, fd3 = -1;
        ~ChlGpioScope() {
            running.store(false);
            if (t2.joinable()) t2.join();
            if (t3.joinable()) t3.join();
            if (fd2 >= 0) ::close(fd2);
            if (fd3 >= 0) ::close(fd3);
        }
    } btn;
    btn.fd2 = gpioOpenCdev(17);
    btn.fd3 = gpioOpenCdev(18);
    if (btn.fd2 >= 0) btn.t2 = std::thread(gpioWatchThread, btn.fd2, &btn.sw2, &btn.running);
    if (btn.fd3 >= 0) btn.t3 = std::thread(gpioWatchThread, btn.fd3, &btn.sw3, &btn.running);

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

        // ── Physical buttons: SW2=CLEAR, SW3=SUBMIT ──────────────────────
        if (btn.sw2.exchange(false)) {
            // SW2: clear answer area
            answerInkWritten = false;
            answerStrokeActive = false;
            haveRecentAnswerEnd = false;
            queuedInkPoints.clear();
            display->beginFrame();
            redrawMyPanelClean();
            display->endFrame();
            sendAnswerControl("A_CLEAR", std::to_string(myPlayerNumber));
            fprintf(stderr, "[Button] SW2 -> CLEAR\n");
        }
        if (btn.sw3.exchange(false)) {
            // SW3: submit answer
            if (!submitted && !submitLocked && answerInkWritten) {
                flushQueuedInk();
                myAnswerInput = "DRAWN";
                broadcastAnswer(myPlayerNumber, myAnswerInput);
                submitted = true;
                fprintf(stderr, "[Button] SW3 -> SUBMIT\n");
                std::cout << "[P" << myPlayerNumber << "] : HANDWRITING (button)\n";
            }
        }

        // Challenger info panel update (once per second)
        {
            auto cnow = std::chrono::steady_clock::now();
            int celapsed = std::chrono::duration_cast<std::chrono::seconds>(cnow - challengerRoundStart).count();
            if (celapsed != challengerLastSec) {
                challengerLastSec = celapsed;
                int remainSec = ROUND_TIMEOUT_SEC - celapsed;
                if (remainSec < 0) remainSec = 0;

                // Restore mood after cry duration
                if (challengerMood == "cry" && cnow >= moodCryUntil) {
                    challengerMood = "normal";
                }

                display->beginFrame();
                drawTimerGauge(remainSec, ROUND_TIMEOUT_SEC, challengerMood);

                // Character image
                int charSzC = std::min(80, topH - 130);
                if (charSzC >= 20) {
                    int cix = (150 - charSzC) / 2;
                    int ciy = topH - charSzC - 4;
                    display->drawRect(cix, ciy, charSzC, charSzC, ui::CARD);
                    std::string cpath = "/mnt/nfs/img/player" + std::to_string(myBoardNum) + "/" + challengerMood + ".ppm";
                    if (!display->drawPNG(cpath, cix, ciy, charSzC, charSzC))
                        display->drawPNG("/mnt/nfs/img/character.ppm", cix, ciy, charSzC, charSzC);
                }

                display->drawRect(4, 28, 142, 16, ui::CARD);
                std::string roundStr = "Round " + std::to_string(round + 1) + "/" + std::to_string(MAX_ROUNDS);
                display->drawText(10, 30, roundStr.c_str(), ui::TEXT_MAIN, 1);

                // Hint in info panel (shown after 30 s)
                if (!hintWord.empty()) {
                    display->drawRect(4, 46, 142, 42, 0x2a1800);
                    display->drawText(6, 48, "HINT:", 0xFFAA00, 2);
                    display->drawText(6, 66, hintWord.c_str(), 0xFFE000, 3);
                }

                display->endFrame();
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
                                int dotSize = (color == Display::COLOR_BLACK) ? 9 : 3;
                                int half = dotSize / 2;
                                display->drawRect(sx - half, sy - half, dotSize, dotSize, color);
                            }
                        } catch (...) {}
                    }
                } else if (kind == "CLEAR") {
                    display->drawRect(canvasX, canvasY, canvasW, canvasH, Display::COLOR_BLACK);
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
                } else if (kind == "A_CLEAR") {
                    try {
                        int pn = std::stoi(value);
                        if (pn == 1 || pn == 2) {
                            display->beginFrame();
                            clearAnswerAreaForSlot(pn);
                            display->endFrame();
                            if (pn != myPlayerNumber) {
                                otherStrokeActive = false;
                            }
                        }
                    } catch (...) {}
                } else if (kind == "STATUS" && (value == "A_CLEAR_P1" || value == "A_CLEAR_P2")) {
                    int pn = (value == "A_CLEAR_P1") ? 1 : 2;
                    display->beginFrame();
                    clearAnswerAreaForSlot(pn);
                    display->endFrame();
                    if (pn != myPlayerNumber) otherStrokeActive = false;
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
                        std::cout << "[P" << myPlayerNumber << "] P" << pn << "  : " << ans << "\n";
                    }
                } else if (kind == "STATUS" && value.rfind("WRONG_ALL", 0) == 0) {

                    std::string revealedAnswer;
                    auto sep = value.find('#');
                    if (sep != std::string::npos) revealedAnswer = value.substr(sep + 1);
                    showTimeUpScreen(revealedAnswer, false);
                    return;
                } else if (kind == "STATUS" && value == "JUDGING_ACTIVE") {

                    if (!submitLocked) {
                        submitLocked = true;
                        redrawSubmitOnly();
                    }
                } else if (kind == "STATUS" && value == "JUDGING_END") {
                    if (submitLocked) {
                        submitLocked = false;
                        redrawSubmitOnly();
                    }
                } else if (kind == "STATUS" && value == "ROUND_END") {
                    std::cout << "[Challenger P" << myPlayerNumber << "] ROUND_END\n";
                    return;
                } else if (kind == "STATUS" && value == "GAME_OVER") {
                    std::cout << "[Challenger P" << myPlayerNumber << "] GAME_OVER\n";
                    showFinalScores();
                    return;
                } else if (kind == "STATUS" && value.rfind("HINT#", 0) == 0) {
                    hintWord = value.substr(5);
                    challengerLastSec = -1;  // force immediate redraw
                } else if (kind == "STATUS" && value.rfind("CORRECT_P", 0) == 0) {
                    int winner = std::stoi(value.substr(9));
                    int winnerBoardNum = winner;
                    auto boardPos = value.find("#BOARD");
                    if (boardPos != std::string::npos) {
                        try { winnerBoardNum = std::stoi(value.substr(boardPos + 6)); } catch (...) {}
                    }
                    bool isEarlyAnswer = (value.find("#EARLY") != std::string::npos);
                    if (winner == myPlayerNumber) {
                        broadcastScoreDelta(nodeId, isEarlyAnswer ? 3 : 2);
                        bgm.playOnce("/mnt/nfs/bgm/correct.wav");
                        isDrawerRole = true;
                        drawerIp.clear();
                        currentDrawerNodeId = nodeId;
                        showCorrectScreen(winnerBoardNum, true, 3000);
                    } else {
                        bgm.playOnce("/mnt/nfs/bgm/correct.wav");
                        isDrawerRole = false;
                        currentDrawerNodeId = senderNodeId;
                        showCorrectScreen(winnerBoardNum, false, 3000);
                    }
                    return;
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
                            if (haveRecentAnswerEnd && recentAnswerAgeMs() > 140) {
                                haveRecentAnswerEnd = false;
                            }

                            if (!answerStrokeActive) {
                                if (haveRecentAnswerEnd) {
                                    int gdx = sx - recentAnswerEndX;
                                    int gdy = sy - recentAnswerEndY;
                                    int gap = std::max(std::abs(gdx), std::abs(gdy));
                                    if (recentAnswerAgeMs() <= 120 && gap <= 48) {
                                        int bsteps = std::max(1, gap);
                                        for (int i = 1; i <= bsteps; ++i) {
                                            int px = recentAnswerEndX + (gdx * i) / bsteps;
                                            int py = recentAnswerEndY + (gdy * i) / bsteps;
                                            display->drawRect(px - 1, py - 1, 2, 2, Display::COLOR_WHITE);
                                            sendAnswerPoint(px, py);
                                        }
                                    }
                                    haveRecentAnswerEnd = false;
                                }

                                display->drawRect(sx - 1, sy - 1, 2, 2, Display::COLOR_WHITE);
                                answerLastX = sx;
                                answerLastY = sy;
                                answerStrokeActive = true;
                                sendAnswerPoint(sx, sy);
                            } else {
                                int dx = sx - answerLastX;
                                int dy = sy - answerLastY;
                                int steps = std::max(std::abs(dx), std::abs(dy));
                                if (steps < 1) steps = 1;
                                for (int i = 1; i <= steps; ++i) {
                                    int px = answerLastX + (dx * i) / steps;
                                    int py = answerLastY + (dy * i) / steps;
                                    display->drawRect(px - 1, py - 1, 2, 2, Display::COLOR_WHITE);
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
                        if (answerStrokeActive) {
                            saveRecentAnswerEnd(answerLastX, answerLastY);
                        }
                        answerStrokeActive = false;
                        if (wasPressed) {
                            sendAnswerUp();
                        }
                        if (wasPressed && touchHasX && touchHasY) {
                            int sx = 0, sy = 0;
                            if (mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                                auto nowTap = std::chrono::steady_clock::now();
                                long long tapGapMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowTap - lastTapTime).count();
                                int tapDx = std::abs(sx - lastTapX);
                                int tapDy = std::abs(sy - lastTapY);
                                if (tapGapMs < 120 && tapDx <= 6 && tapDy <= 6) {
                                    continue;
                                }
                                lastTapTime = nowTap;
                                lastTapX = sx;
                                lastTapY = sy;
                                saveRecentAnswerEnd(sx, sy);
                                if (sx >= clearX && sx < clearX + clearW && sy >= clearY && sy < clearY + clearH) {
                                    answerInkWritten = false;
                                    answerStrokeActive = false;
                                    haveRecentAnswerEnd = false;
                                    queuedInkPoints.clear();
                                    display->beginFrame();
                                    redrawMyPanelClean();
                                    display->endFrame();
                                    sendAnswerControl("A_CLEAR", std::to_string(myPlayerNumber));
                                    continue;
                                }
                                if (sx >= btnX && sx < btnX + btnW && sy >= btnY && sy < btnY + btnH) {
                                    if (!submitted && !submitLocked && answerInkWritten) {
                                        flushQueuedInk();
                                        myAnswerInput = "DRAWN";
                                        broadcastAnswer(myPlayerNumber, myAnswerInput);
                                        submitted = true;
                                        std::cout << "[P" << myPlayerNumber << "] : HANDWRITING\n";
                                    }
                                }
                            }
                        }
                    }
                } else if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) {
                    bool wasPressed = touchPressed;
                    touchPressed = (ev.value >= 0);
                    if (wasPressed && !touchPressed) {
                        if (answerStrokeActive) {
                            saveRecentAnswerEnd(answerLastX, answerLastY);
                        }
                        answerStrokeActive = false;
                        sendAnswerUp();
                        int sx = 0, sy = 0;
                        if (touchHasX && touchHasY && mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                            auto nowTap = std::chrono::steady_clock::now();
                            long long tapGapMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowTap - lastTapTime).count();
                            int tapDx = std::abs(sx - lastTapX);
                            int tapDy = std::abs(sy - lastTapY);
                            if (tapGapMs < 120 && tapDx <= 6 && tapDy <= 6) {
                                continue;
                            }
                            lastTapTime = nowTap;
                            lastTapX = sx;
                            lastTapY = sy;
                            saveRecentAnswerEnd(sx, sy);
                            if (sx >= clearX && sx < clearX + clearW && sy >= clearY && sy < clearY + clearH) {
                                answerInkWritten = false;
                                answerStrokeActive = false;
                                haveRecentAnswerEnd = false;
                                queuedInkPoints.clear();
                                display->beginFrame();
                                redrawMyPanelClean();
                                display->endFrame();
                                sendAnswerControl("A_CLEAR", std::to_string(myPlayerNumber));
                                continue;
                            }
                            if (sx >= btnX && sx < btnX + btnW && sy >= btnY && sy < btnY + btnH) {
                                if (!submitted && !submitLocked && answerInkWritten) {
                                    flushQueuedInk();
                                    myAnswerInput = "DRAWN";
                                    broadcastAnswer(myPlayerNumber, myAnswerInput);
                                    submitted = true;
                                    std::cout << "[P" << myPlayerNumber << "] : HANDWRITING\n";
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
                std::cout << "[P" << myPlayerNumber << "] : HANDWRITING\n";
            }
        }
    }

    std::cout << "[P" << myPlayerNumber << "]  .   ...\n";
    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;
        if (roleSock >= 0) { FD_SET(roleSock, &readfds); if (roleSock > maxfd) maxfd = roleSock; }

        timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 50000;
        int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) { if (errno == EINTR) continue; break; }


        {
            auto cnow = std::chrono::steady_clock::now();
            int celapsed = std::chrono::duration_cast<std::chrono::seconds>(cnow - challengerRoundStart).count();
            if (celapsed != challengerLastSec) {
                challengerLastSec = celapsed;
                int remainSec = ROUND_TIMEOUT_SEC - celapsed;
                if (remainSec < 0) remainSec = 0;

                if (challengerMood == "cry" && cnow >= moodCryUntil) {
                    challengerMood = "normal";
                }


                display->beginFrame();
                drawTimerGauge(remainSec, ROUND_TIMEOUT_SEC, challengerMood);

                // Character image
                int charSzW = std::min(80, topH - 130);
                if (charSzW >= 20) {
                    int cix = (150 - charSzW) / 2;
                    int ciy = topH - charSzW - 4;
                    display->drawRect(cix, ciy, charSzW, charSzW, ui::CARD);
                    std::string cpath = "/mnt/nfs/img/player" + std::to_string(myBoardNum) + "/" + challengerMood + ".ppm";
                    if (!display->drawPNG(cpath, cix, ciy, charSzW, charSzW))
                        display->drawPNG("/mnt/nfs/img/character.ppm", cix, ciy, charSzW, charSzW);
                }

                // Hint in info panel (shown after 30 s)
                if (!hintWord.empty()) {
                    display->drawRect(4, 46, 142, 42, 0x2a1800);
                    display->drawText(6, 48, "HINT:", 0xFFAA00, 2);
                    display->drawText(6, 66, hintWord.c_str(), 0xFFE000, 3);
                }

                display->endFrame();
            }
        }

        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind, value, senderIp, senderNodeId;
            while (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId == nodeId) continue;
                if (kind == "STATUS") {
                    if (value.rfind("WRONG_ALL", 0) == 0) {
                        std::string revealedAnswer;
                        auto sep = value.find('#');
                        if (sep != std::string::npos) revealedAnswer = value.substr(sep + 1);
                        showTimeUpScreen(revealedAnswer, false);
                        return;
                    } else if (value.rfind("HINT#", 0) == 0) {
                        hintWord = value.substr(5);
                        challengerLastSec = -1;  // force redraw
                    } else if (value == "ROUND_END") {
                        std::cout << "[Challenger P" << myPlayerNumber << "] ROUND_END (wait)\n";
                        return;
                    } else if (value == "GAME_OVER") {
                        std::cout << "[Challenger] GAME_OVER -> final scores\n";
                        showFinalScores();
                        return;
                    } else if ((value == "RETRY_P1" && myPlayerNumber == 1) ||
                               (value == "RETRY_P2" && myPlayerNumber == 2)) {
                        std::cout << "[P" << myPlayerNumber << "] NG  ->    \n";
                        bgm.playOnce("/mnt/nfs/bgm/incorrect.wav");
                        challengerMood = "cry";
                        moodCryUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                        myAnswerInput.clear();
                        answerInkWritten = false;
                        answerStrokeActive = false;
                        submitLocked = false;  // ensure SUBMIT is re-enabled
                        queuedInkPoints.clear();
                        display->beginFrame();
                        redrawPanels();
                        display->endFrame();
                        goto input_phase;
                    } else if (value == "JUDGING_ACTIVE") {
                        if (!submitLocked) {
                            submitLocked = true;
                            redrawSubmitOnly();
                        }
                    } else if (value == "JUDGING_END") {
                        if (submitLocked) {
                            submitLocked = false;
                            redrawSubmitOnly();
                        }
                    } else if (value.rfind("CORRECT_P", 0) == 0) {
                        int winner = std::stoi(value.substr(9));
                        int winnerBoardNum = winner;
                        auto boardPos = value.find("#BOARD");
                        if (boardPos != std::string::npos) {
                            try { winnerBoardNum = std::stoi(value.substr(boardPos + 6)); } catch (...) {}
                        }
                        bool isEarlyAnswer = (value.find("#EARLY") != std::string::npos);
                        if (winner == myPlayerNumber) {
                            broadcastScoreDelta(nodeId, isEarlyAnswer ? 3 : 2);
                            bgm.playOnce("/mnt/nfs/bgm/correct.wav");
                            isDrawerRole = true;
                            drawerIp.clear();
                            currentDrawerNodeId = nodeId;
                            showCorrectScreen(winnerBoardNum, true, 3000);
                        } else {
                            bgm.playOnce("/mnt/nfs/bgm/correct.wav");
                            isDrawerRole = false;
                            currentDrawerNodeId = senderNodeId;
                            showCorrectScreen(winnerBoardNum, false, 3000);
                        }
                        return;
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
                                sy >= canvasY && sy < canvasY + canvasH) {
                                int dotSize = (color == Display::COLOR_BLACK) ? 9 : 3;
                                int half = dotSize / 2;
                                display->drawRect(sx - half, sy - half, dotSize, dotSize, color);
                            }
                        } catch (...) {}
                    }
                } else if (kind == "CLEAR") {
                    display->drawRect(canvasX, canvasY, canvasW, canvasH, Display::COLOR_BLACK);
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
                } else if (kind == "A_CLEAR") {
                    try {
                        int pn = std::stoi(value);
                        if (pn == 1 || pn == 2) {
                            display->beginFrame();
                            clearAnswerAreaForSlot(pn);
                            display->endFrame();
                            if (pn != myPlayerNumber) {
                                otherStrokeActive = false;
                            }
                        }
                    } catch (...) {}
                } else if (kind == "STATUS" && (value == "A_CLEAR_P1" || value == "A_CLEAR_P2")) {
                    int pn = (value == "A_CLEAR_P1") ? 1 : 2;
                    display->beginFrame();
                    clearAnswerAreaForSlot(pn);
                    display->endFrame();
                    if (pn != myPlayerNumber) otherStrokeActive = false;
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

    drawBg(display, screenW, screenH, 1);

    int cx = screenW / 2;


    std::string label = toDisplayLabel(selectedText);
    int cardW = screenW * 3 / 4;
    int cardH = 110;
    int cardX = cx - cardW / 2;
    int cardY = screenH / 2 - 120;
    display->drawRect(cardX, cardY, cardW, cardH, ui::BG_DARK);
    drawPanelCard(display, cardX, cardY, cardW, cardH, ui::ACCENT_WARM, ui::CARD, ui::BG_MID);
    drawTextCentered(display, cx, cardY + 16, "CONFIRM SELECTION", ui::ACCENT_WARM, 1);
    drawTextCentered(display, cx, cardY + 44, label, ui::TEXT_MAIN, 3);


    int btnMargin = 16;
    int btnGap = 24;
    int btnW = (screenW - (btnMargin * 2) - btnGap) / 2;
    int btnH = 78;
    int leftBtnX = btnMargin;
    int rightBtnX = btnMargin + btnW + btnGap;
    int btnY = screenH - btnH - 18;

    display->drawRect(0, btnY - 12, screenW, btnH + 30, ui::BG_DARK);

    drawPanelCard(display, leftBtnX - 2, btnY - 2, btnW + 4, btnH + 4, ui::OK, 0x1c492d, 0x1a3e29);
    drawTextCentered(display, leftBtnX + (btnW / 2), btnY + 22, "O", ui::OK, 3);

    drawPanelCard(display, rightBtnX - 2, btnY - 2, btnW + 4, btnH + 4, ui::NG, 0x4f2222, 0x3f1e1e);
    drawTextCentered(display, rightBtnX + (btnW / 2), btnY + 22, "X", ui::NG, 3);
    display->endFrame();

    std::cout << "[] " << selectedText << "  . (1=, 2=)\n";


    while (true) {
        int sx = 0;
        int sy = 0;
        if (waitTouchReleasePoint(sx, sy, 200)) {
            if (sx >= leftBtnX && sx < leftBtnX + btnW && sy >= btnY && sy < btnY + btnH) {
                std::cout << "[]  \n";
                return true;
            }
            if (sx >= rightBtnX && sx < rightBtnX + btnW && sy >= btnY && sy < btnY + btnH) {
                std::cout << "[]  \n";
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
                std::cout << "[]  \n";
                return true;
            }
            if (line == "2") {
                std::cout << "[]  \n";
                return false;
            }
        }
    }
}

bool CatchMindGame::selectCategoryAndWord() {

    // Flush any residual touch events from the previous screen
    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX    = false;
        touchHasY    = false;
    }
    usleep(300000);  // 300 ms grace period

    std::vector<std::string> allCategories;
    for (const auto &kv : wordBank) {
        allCategories.push_back(kv.first);
    }
    // Pick 3 random categories
    std::vector<std::string> categories = pickRandomWords(allCategories, 3);
    std::sort(categories.begin(), categories.end());


    while (true) {
        int catSelected = -1;
        if (!selectFromTouchMenu("CATEGORY", categories, catSelected, 0x004488, "WORD_SELECTING")) {
            return false;
        }
        if (catSelected < 0 || catSelected >= (int)categories.size()) {
            return false;
        }

        currentCategory = categories[(size_t)catSelected];


        if (!showConfirmDialog(currentCategory)) {
            continue;  //  ->  
        }
        break;  //  ->  
    }

    offeredWords = pickRandomWords(wordBank[currentCategory], 4);


    while (true) {
        int wordSelected = -1;
        if (!selectFromTouchMenu("WORD", offeredWords, wordSelected, 0x664400, "WORD_SELECTING")) {
            return false;
        }
        if (wordSelected < 0 || wordSelected >= (int)offeredWords.size()) {
            return false;
        }

        targetWord = offeredWords[(size_t)wordSelected];


        if (!showConfirmDialog(targetWord)) {
            continue;  //  ->  
        }
        break;  //  ->  
    }

    std::cout << "[]  : " << targetWord << "\n";

    // ①
    broadcastStatusMessage("DRAWING_START");  // 150ms
    broadcastStatusMessage("DRAWING_START");  // 150ms
    broadcastStatusMessage("DRAWING_START");  // 150ms
    broadcastStatusMessage("DRAWING_START");  // 150ms =  600ms

    // ②
    showTransitionScreen("GAME START", "GET READY!", 1500);

    // ③
    broadcastStatusMessage("GAME_READY");
    return true;
}

void CatchMindGame::printRoundGuide() {
    std::cout << "\n[ ]\n";
    std::cout << "  touch drag    : \n";
    std::cout << "  top palette   :   / eraser / clear\n";
    std::cout << "  p             :  on/off\n";
    std::cout << "  e             :  \n";
    std::cout << "  c             :  \n";
    std::cout << "  1~7           : \n";
    std::cout << "  0             : \n";
    std::cout << "  ok1/ng1       : P1 / \n";
    std::cout << "  ok2/ng2       : P2 / \n";
    std::cout << "  q             :  \n\n";
    std::cout << "[ ]\n";
    std::cout << "  answer <>   :  \n";
    std::cout << "  submit        : \n\n";
}


int CatchMindGame::getPlayerNumberFromIp(const std::string &ip) {



    if (ip.find("192.168.10.3") != std::string::npos) return 1;
    if (ip.find("192.168.10.4") != std::string::npos) return 2;
    if (ip.find("192.168.10.5") != std::string::npos) return 3;
    return 0;  // 
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


    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX = false;
        touchHasY = false;
        strokeActive = false;
    }

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
    const int eraseW = 86;
    const int eraseH = 32;
    const int clearW = 86;
    const int clearH = 32;
    const int toolsY = 2;
    const int paletteW = (8 * swatchSize) + (7 * swatchGap);
    const int toolsX = std::max(8, screenW - paletteW - eraseW - clearW - 32);
    const int eraseX = toolsX + paletteW + 8;
    const int clearX = eraseX + eraseW + 8;

    auto drawDrawerTools = [&]() {
        display->drawRect(toolsX - 6, toolsY - 3, paletteW + eraseW + clearW + 30, clearH + 7, ui::BG_MID);
        for (int i = 0; i < 8; ++i) {
            int x = toolsX + i * (swatchSize + swatchGap);
            unsigned int edge = (brushColor == rainbowColors[i]) ? ui::ACCENT : ui::STROKE;
            drawPanelCard(display, x, toolsY, swatchSize, swatchSize, edge, rainbowColors[i], rainbowColors[i]);
        }

        bool eraserOn = (brushColor == Display::COLOR_BLACK);
        unsigned int eraserEdge = eraserOn ? ui::ACCENT : ui::STROKE;
        unsigned int eraserText = eraserOn ? ui::ACCENT : ui::TEXT_DIM;
        drawPanelCard(display, eraseX, toolsY, eraseW, eraseH, eraserEdge, 0x22282f, 0x1a2027);
        display->drawText(eraseX + 14, toolsY + 12, "ERASE", eraserText, 1);

        display->drawRect(eraseX + eraseW - 24, toolsY + 9, 13, 1, Display::COLOR_WHITE);
        display->drawRect(eraseX + eraseW - 24, toolsY + 21, 13, 1, Display::COLOR_WHITE);
        display->drawRect(eraseX + eraseW - 24, toolsY + 9, 1, 13, Display::COLOR_WHITE);
        display->drawRect(eraseX + eraseW - 12, toolsY + 9, 1, 13, Display::COLOR_WHITE);

        drawPanelCard(display, clearX, toolsY, clearW, clearH, ui::ACCENT_WARM, 0x533611, 0x3c280f);
        display->drawText(clearX + 18, toolsY + 12, "CLEAR", ui::ACCENT_WARM, 1);
    };

    auto drawJudgeButtonsFor = [&](int playerNum, bool visible) {
        int halfW = screenW / 2;
        int panelX = (playerNum == 1) ? 0 : halfW;
        int panelW = (playerNum == 1) ? halfW : (screenW - halfW);

        int btnW = 130;
        int btnH = 48;
        int gap  = 12;
        int totalW = btnW * 2 + gap;
        int btnY = bottomY + panelH - btnH - 8;
        int okX = panelX + (panelW - totalW) / 2;
        int ngX = okX + btnW + gap;

        if (!visible) {
            display->drawRect(okX, btnY, btnW, btnH, ui::CARD_ALT);
            display->drawRect(ngX, btnY, btnW, btnH, ui::CARD_ALT);
            return;
        }

        drawPanelCard(display, okX, btnY, btnW, btnH, ui::OK, 0x1c492d, 0x1a3e29);
        drawTextCentered(display, okX + btnW / 2, btnY + btnH / 2 - 8, "OK", ui::OK, 2);

        drawPanelCard(display, ngX, btnY, btnW, btnH, ui::NG, 0x4f2222, 0x3f1e1e);
        drawTextCentered(display, ngX + btnW / 2, btnY + btnH / 2 - 8, "NG", ui::NG, 2);
    };

    auto getJudgeButtonRects = [&](int playerNum, int &okX, int &ngX, int &btnY, int &btnW, int &btnH) {
        int halfW = screenW / 2;
        int panelX = (playerNum == 1) ? 0 : halfW;
        int panelW = (playerNum == 1) ? halfW : (screenW - halfW);
        btnW = 130;
        btnH = 48;
        int gap = 12;
        int totalW = btnW * 2 + gap;
        btnY = bottomY + panelH - btnH - 8;
        okX = panelX + (panelW - totalW) / 2;
        ngX = okX + btnW + gap;
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
        int areaY = bottomY + 4;
        int areaW = std::max(10, panelW - 16);
        int areaH = std::max(10, panelH - 4 - 48 - 8 - 4);  // top4 + btnH48 + gap8 + bot4

        int sx = areaX + (nx * (areaW - 1)) / 999;
        int sy = areaY + (ny * (areaH - 1)) / 999;

        bool &active = (playerNum == 1) ? answerStrokeActive1 : answerStrokeActive2;
        int &lx = (playerNum == 1) ? answerLastDrawX1 : answerLastDrawX2;
        int &ly = (playerNum == 1) ? answerLastDrawY1 : answerLastDrawY2;

        if (!active) {
            display->drawRect(sx - 1, sy - 1, 2, 2, Display::COLOR_WHITE);
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
            display->drawRect(px - 1, py - 1, 2, 2, Display::COLOR_WHITE);
        }
        lx = sx;
        ly = sy;
    };

    std::cout << "[]   \n";
    std::cout << "[] =" << currentCategory << "\n";
    std::cout << "[] =" << targetWord << " ( )\n";
    if (touchFd < 0) {
        std::cout << "[]    .\n";
    } else {
        std::cout << "[]   \n";
    }
    broadcastStatusMessage("DRAWING_ACTIVE");
    printRoundGuide();

    drawDrawerTools();
    display->endFrame();

    std::string line;
    bool judgingActive = false;
    bool roundEnded = false;
    int  pendingJudgePlayer = 0;

    // GPIO button setup (SW2=line17 → OK, SW3=line18 → NG)
    int sw2Fd = gpioOpenCdev(17);
    int sw3Fd = gpioOpenCdev(18);
    std::atomic<bool> sw2Pressed{false};
    std::atomic<bool> sw3Pressed{false};
    std::atomic<bool> btnRunning{true};
    std::thread sw2Thread, sw3Thread;
    if (sw2Fd >= 0) sw2Thread = std::thread(gpioWatchThread, sw2Fd, &sw2Pressed, &btnRunning);
    if (sw3Fd >= 0) sw3Thread = std::thread(gpioWatchThread, sw3Fd, &sw3Pressed, &btnRunning);

    // Slot index (1/2)
    std::unordered_map<int, int> slotToBoard;


    auto roundStartTime = std::chrono::steady_clock::now();
    const int ROUND_TIMEOUT_SEC = 60;

    auto judgeOk = [&](int playerNum) {
        display->beginFrame();
        paintAnswerPanel(playerNum, (playerNum == 1) ? 0x102336 : 0x2a1b21);
        display->endFrame();
        bgm.playOnce("/mnt/nfs/bgm/correct.wav");
        int elapsedJudge = (int)std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - roundStartTime).count();
        bool isEarlyAnswer = (elapsedJudge < 30);
        broadcastScoreDelta(nodeId, isEarlyAnswer ? 2 : 1);
        int winnerBoardNum = slotToBoard.count(playerNum) ? slotToBoard[playerNum] : playerNum;
        std::string correctMsg = "CORRECT_P" + std::to_string(playerNum)
                                + "#BOARD" + std::to_string(winnerBoardNum);
        correctMsg += isEarlyAnswer ? "#EARLY" : "#LATE";
        broadcastStatusMessage(correctMsg);
        showCorrectScreen(winnerBoardNum, false, 3000);
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
            display->beginFrame();
            paintAnswerPanel(1, 0x102336);
            display->endFrame();
            broadcastStatusMessage("A_CLEAR_P1");  //   P1   
            broadcastStatusMessage("RETRY_P1");
        } else {
            receivedAnswer2.clear();
            answerReceived2 = false;
            answerStrokeActive2 = false;
            display->beginFrame();
            paintAnswerPanel(2, 0x2a1b21);
            display->endFrame();
            broadcastStatusMessage("A_CLEAR_P2");  //   P2   
            broadcastStatusMessage("RETRY_P2");
        }
        judgingActive = false;
        pendingJudgePlayer = 0;
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

        // ── Hardware buttons: atomic flags set by background threads ──────────
        if (judgingActive && pendingJudgePlayer > 0 && !roundEnded) {
            if (sw2Pressed.exchange(false)) {
                std::cout << "[Button] SW2 OK -> P" << pendingJudgePlayer << "\n";
                judgeOk(pendingJudgePlayer);
            } else if (sw3Pressed.exchange(false)) {
                std::cout << "[Button] SW3 NG -> P" << pendingJudgePlayer << "\n";
                judgeNg(pendingJudgePlayer);
            }
        } else {
            sw2Pressed.store(false);  // discard spurious presses outside judging
            sw3Pressed.store(false);
        }


        auto now = std::chrono::steady_clock::now();
        int elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(now - roundStartTime).count();
        if (elapsedSec >= ROUND_TIMEOUT_SEC && !roundEnded) {
            std::cout << "[] 60  ->  ,  \n";
            broadcastStatusMessage("JUDGING_ACTIVE");
            broadcastStatusMessage("WRONG_ALL#" + targetWord); // #    
            broadcastStatusMessage("JUDGING_END");
            broadcastScoreDelta(nodeId, -1);   // timeout:  -1
            showTimeUpScreen(targetWord, true);
            broadcastStatusMessage("ROUND_END");
            isDrawing = false;
            roundEnded = true;
        }

        // INFO panel update (once per second)
        static int lastDisplayedSec = -1;
        if (elapsedSec != lastDisplayedSec) {
            lastDisplayedSec = elapsedSec;

            int remainSec = ROUND_TIMEOUT_SEC - elapsedSec;
            if (remainSec < 0) remainSec = 0;

            // Broadcast hint (category) at 30-second mark
            if (elapsedSec == 30) {
                broadcastStatusMessage("HINT#" + currentCategory);
            }

            display->beginFrame();
            std::string drawerMood = (remainSec > 30) ? "smile" : (remainSec > 10 ? "normal" : "cry");
            drawTimerGauge(remainSec, ROUND_TIMEOUT_SEC, drawerMood);


            int charSzD = std::min(80, topH - 130);
            if (charSzD >= 20) {
                int cix = (150 - charSzD) / 2;
                int ciy = topH - charSzD - 4;
                display->drawRect(cix, ciy, charSzD, charSzD, ui::CARD);
                std::string cpath = "/mnt/nfs/img/player" + std::to_string(myBoardNum) + "/" + drawerMood + ".ppm";
                if (!display->drawPNG(cpath, cix, ciy, charSzD, charSzD))
                    display->drawPNG("/mnt/nfs/img/character.ppm", cix, ciy, charSzD, charSzD);
            }


            display->drawRect(4, 28, 142, 16, ui::CARD);
            std::string roundStr = "Round " + std::to_string(round + 1) + "/" + std::to_string(MAX_ROUNDS);
            display->drawText(10, 30, roundStr.c_str(), ui::TEXT_MAIN, 1);


            display->drawRect(4, 46, 142, 34, 0x0b2e20);
            display->drawRect(4, 46, 142, 14, ui::STROKE);
            display->drawText(8, 48, "WORD:", ui::TEXT_DIM, 0);
            std::string wordLabel = toDisplayLabel(targetWord);
            display->drawText(8, 62, wordLabel.substr(0, 12).c_str(), 0xFFE000, 1);


            int statusY = 84;
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
            display->endFrame();
        }

        if (touchFd >= 0 && FD_ISSET(touchFd, &readfds)) {
            bool released = false;
            int rsx = 0, rsy = 0;
            processTouchEvents(&released, &rsx, &rsy);


            if (released) {
                bool toolHandled = false;
                for (int i = 0; i < 8; ++i) {
                    int x = toolsX + i * (swatchSize + swatchGap);
                    if (rsx >= x && rsx < x + swatchSize && rsy >= toolsY && rsy < toolsY + swatchSize) {
                        brushColor = rainbowColors[i];
                        drawDrawerTools();
                        std::cout << "[]    index=" << (i + 1) << "\n";
                        toolHandled = true;
                        break;
                    }
                }
                if (toolHandled) {
                    continue;
                }

                if (rsx >= eraseX && rsx < eraseX + eraseW && rsy >= toolsY && rsy < toolsY + eraseH) {
                    brushColor = Display::COLOR_BLACK;
                    drawDrawerTools();
                    std::cout << "[]  \n";
                    continue;
                }

                if (rsx >= clearX && rsx < clearX + clearW && rsy >= toolsY && rsy < toolsY + clearH) {
                    resetCanvas();
                    drawBrushDot(cursorX, cursorY);
                    drawDrawerTools();
                    broadcastCanvasClear();
                    std::cout << "[]  ( )\n";
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


        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind, value, senderIp, senderNodeId;
            while (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId == nodeId) continue;
                if (kind == "ANSWER") {
                    if (judgingActive) {
                        std::cout << "[] 판정 중 ANSWER 무시\n";
                        continue;
                    }
                    auto sep = value.find(':');
                    if (sep != std::string::npos) {
                        int pn = std::stoi(value.substr(0, sep));
                        std::string ans = value.substr(sep + 1);
                        slotToBoard[pn] = getPlayerNumberFromIp(senderIp);
                        if (pn == 1) { receivedAnswer1 = ans; answerReceived1 = true; }
                        if (pn == 2) { receivedAnswer2 = ans; answerReceived2 = true; }
                        std::cout << "[] P" << pn << "  : " << ans
                                  << " (IP=" << senderIp << ", =" << slotToBoard[pn] << ")\n";


                        judgingActive = true;
                        pendingJudgePlayer = pn;
                        broadcastStatusMessage("JUDGING_ACTIVE");

                        // Submit highlight: panel border + top banner
                        display->beginFrame();
                        {
                            int hw = screenW / 2;
                            int pnlX  = (pn == 1) ? 0   : hw;
                            int pnlW  = (pn == 1) ? hw  : (screenW - hw);
                            unsigned int hlColor = (pn == 1) ? ui::P1_ACCENT : ui::P2_ACCENT;

                            for (int t = 0; t < 3; ++t) {
                                display->drawRect(pnlX + t,     bottomY + t, pnlW - t*2, 1,       hlColor);
                                display->drawRect(pnlX + t,     bottomY + t, 1,          panelH - t*2, hlColor);
                                display->drawRect(pnlX + t,     bottomY + panelH - 1 - t, pnlW - t*2, 1, hlColor);
                                display->drawRect(pnlX + pnlW - 1 - t, bottomY + t, 1,  panelH - t*2, hlColor);
                            }

                            int bannerH = 22;
                            display->drawRect(pnlX + 4, bottomY + 4, pnlW - 8, bannerH, hlColor);
                            std::string banner = "P" + std::to_string(pn) + " SUBMITTED!";
                            int bLen = (int)banner.size() * 6 * 2;
                            display->drawText(pnlX + (pnlW - bLen) / 2, bottomY + 8, banner, 0x000000, 2);
                        }
                        drawJudgeButtonsFor(pn, true);
                        display->endFrame();
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
                } else if (kind == "A_CLEAR") {
                    try {
                        int pn = std::stoi(value);
                        display->beginFrame();
                        if (pn == 1) {
                            answerStrokeActive1 = false;
                            paintAnswerPanel(1, 0x102336);
                        }
                        if (pn == 2) {
                            answerStrokeActive2 = false;
                            paintAnswerPanel(2, 0x2a1b21);
                        }
                        display->endFrame();
                    } catch (...) {}
                } else if (kind == "STATUS" && (value == "A_CLEAR_P1" || value == "A_CLEAR_P2")) {
                    display->beginFrame();
                    if (value == "A_CLEAR_P1") { answerStrokeActive1 = false; paintAnswerPanel(1, 0x102336); }
                    else                        { answerStrokeActive2 = false; paintAnswerPanel(2, 0x2a1b21); }
                    display->endFrame();
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
                } else if (kind == "STATUS" && value == "GAME_OVER") {
                    // Another board broadcast GAME_OVER first
                    std::cout << "[Challenger] GAME_OVER -> final scores\n";
                    isDrawing = false;
                    roundEnded = true;
                    showFinalScores();
                    return;
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (!std::getline(std::cin, line)) { isDrawing = false; break; }

            if (line == "q") {
                std::cout << "[]  \n";
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


    if (!roundEnded) {
        if (judgingActive) broadcastStatusMessage("JUDGING_END");
        broadcastStatusMessage("ROUND_END");
        sleep(1);
    }

    // Stop button threads and close GPIO fds
    btnRunning.store(false);
    if (sw2Thread.joinable()) sw2Thread.join();
    if (sw3Thread.joinable()) sw3Thread.join();
    if (sw2Fd >= 0) close(sw2Fd);
    if (sw3Fd >= 0) close(sw3Fd);

    // Flush touch buffer so residual taps don't bleed into category selection
    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX    = false;
        touchHasY    = false;
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
        std::cout << "[] " << playerIndex << " ='" << answer << "' => \n";
        paintAnswerPanel(playerIndex, Display::COLOR_GREEN);
        display->drawRect(canvasX - 3, canvasY - 3, canvasW + 6, canvasH + 6, Display::COLOR_GREEN);
        std::cout << "[] !    \n";
        

        isDrawerRole = true;
        drawerIp.clear();
        currentDrawerNodeId = nodeId;
        
        broadcastStatusMessage("ROUND_END");
        sleep(2);
        return true;
    }

    std::cout << "[] " << playerIndex << " ='" << answer << "' => \n";
    paintAnswerPanel(playerIndex, Display::COLOR_RED);
    return false;
}

bool CatchMindGame::handleDrawCommand(const std::string &cmd) {
    if (display == nullptr || cmd.empty()) {
        return false;
    }

    if (cmd == "p") {
        penDown = !penDown;
        std::cout << "[] =" << (penDown ? "on" : "off") << "\n";
        return true;
    }
    if (cmd == "c") {
        resetCanvas();
        drawBrushDot(cursorX, cursorY);
        broadcastCanvasClear();
        std::cout << "[]  \n";
        return true;
    }
    if (cmd == "e") {
        brushColor = Display::COLOR_BLACK;
        std::cout << "[]  \n";
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

    std::cout << "[]    \n";
    return false;
}

void CatchMindGame::drawBrushDot(int x, int y) {
    if (display == nullptr) {
        return;
    }
    int dotSize = (brushColor == Display::COLOR_BLACK) ? 9 : 3;
    int half = dotSize / 2;
    display->drawRect(x - half, y - half, dotSize, dotSize, brushColor);
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



    static bool haveRecentStrokeEnd = false;
    static int recentStrokeEndX = 0;
    static int recentStrokeEndY = 0;
    static std::chrono::steady_clock::time_point recentStrokeEndTime = std::chrono::steady_clock::now();

    auto ageRecentEndMs = [&]() -> long long {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - recentStrokeEndTime).count();
    };

    auto saveRecentEnd = [&](int sx, int sy) {
        recentStrokeEndX = sx;
        recentStrokeEndY = sy;
        recentStrokeEndTime = std::chrono::steady_clock::now();
        haveRecentStrokeEnd = true;
    };

    static auto lastReleaseTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(300);
    static int lastReleaseX = -10000;
    static int lastReleaseY = -10000;

    auto emitRelease = [&](int sx, int sy) {
        auto nowRel = std::chrono::steady_clock::now();
        long long dtMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowRel - lastReleaseTime).count();
        int dx = std::abs(sx - lastReleaseX);
        int dy = std::abs(sy - lastReleaseY);
        if (dtMs < 120 && dx <= 6 && dy <= 6) {
            return;
        }
        lastReleaseTime = nowRel;
        lastReleaseX = sx;
        lastReleaseY = sy;
        if (released != nullptr) {
            *released = true;
        }
        if (releaseX != nullptr) *releaseX = sx;
        if (releaseY != nullptr) *releaseY = sy;
    };

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
                    if (strokeActive) {
                        saveRecentEnd(strokeLastX, strokeLastY);
                    }
                    strokeActive = false;
                    if (released != nullptr && wasPressed && touchHasX && touchHasY) {
                        int sx = 0, sy = 0;
                        if (mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                            saveRecentEnd(sx, sy);
                            emitRelease(sx, sy);
                        }
                    }
                }
            }
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            bool wasPressed = touchPressed;
            touchPressed = (ev.value != 0);
            if (!touchPressed) {
                if (strokeActive) {
                    saveRecentEnd(strokeLastX, strokeLastY);
                }
                strokeActive = false;
                if (released != nullptr && wasPressed && touchHasX && touchHasY) {
                    int sx = 0, sy = 0;
                    if (mapTouchToScreen(touchRawX, touchRawY, sx, sy)) {
                        saveRecentEnd(sx, sy);
                        emitRelease(sx, sy);
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

            if (haveRecentStrokeEnd && ageRecentEndMs() > 140) {
                haveRecentStrokeEnd = false;
            }

            if (!strokeActive) {
                if (haveRecentStrokeEnd) {
                    int gapDx = sx - recentStrokeEndX;
                    int gapDy = sy - recentStrokeEndY;
                    int gap = std::max(std::abs(gapDx), std::abs(gapDy));


                    if (ageRecentEndMs() <= 120 && gap <= 48) {
                        int bridgeSteps = std::max(1, gap);
                        for (int i = 1; i <= bridgeSteps; ++i) {
                            int px = recentStrokeEndX + (gapDx * i) / bridgeSteps;
                            int py = recentStrokeEndY + (gapDy * i) / bridgeSteps;
                            drawBrushDot(px, py);
                            broadcastDrawPoint(px, py, brushColor);
                        }
                    }
                    haveRecentStrokeEnd = false;
                }

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


std::string CatchMindGame::getLocalIpAddress() {
    struct ifaddrs *ifaddr = nullptr, *ifa = nullptr;
    char host[NI_MAXHOST];
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return "";
    }
    
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        

        if (ifa->ifa_addr->sa_family == AF_INET) {
            int ret = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), 
                                  host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
            if (ret == 0) {
                std::string ip(host);

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




void CatchMindGame::showTimeUpScreen(const std::string &answer, bool isDrawer) {
    if (display == nullptr) return;

    display->beginFrame();


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


    int btnW = 180, btnH = 44;
    int btnX = cx - btnW / 2;
    int btnY = by + bh + 24;
    drawPanelCard(display, btnX, btnY, btnW, btnH, ui::OK, 0x1c492d, 0x1a3e29);
    drawTextCentered(display, cx, btnY + 14, isDrawer ? "WAITING CHALLENGERS" : "TAP TO CONFIRM", ui::OK, 1);

    int waitY = btnY + btnH + 16;
    display->endFrame();

    bool myConfirmed = isDrawer; //     
    bool readySent = false;
    int othersReady = 0;
    bool roundEndReceived = false;
    std::vector<std::string> readyNodes;
    const int OTHERS_NEEDED = isDrawer ? 2 : 1; // : 2, :  1

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


        display->beginFrame();
        display->drawRect(cx - 200, waitY, 400, 20, ui::BG_DARK);
        std::string waitMsg;
        if (!myConfirmed) {
            waitMsg = "Tap button to confirm";
        } else {
            waitMsg = "Waiting... (" + std::to_string(othersReady) + "/" + std::to_string(OTHERS_NEEDED) + ")";
        }
        drawTextCentered(display, cx, waitY, waitMsg, ui::TEXT_DIM, 1);
        display->endFrame();

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
                        std::cout << "[] READY_NEXT  (" << othersReady << "/" << OTHERS_NEEDED << ")\n";
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

    // All boards confirmed
    display->beginFrame();
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

    drawBg(display, screenW, screenH, 3);

    int cx = screenW / 2;
    int cy = screenH / 2;

    bool isCorrect = (line1.find("CORRECT") != std::string::npos || line1.find("READY") != std::string::npos);
    bool isNeg     = (line1.find("OVER") != std::string::npos || line1.find("TIME") != std::string::npos);
    unsigned int edge = isCorrect ? ui::OK : (isNeg ? ui::NG : ui::ACCENT_WARM);
    unsigned int fillOuter = isCorrect ? 0x0d2418 : (isNeg ? 0x200a0a : 0x1a1230);

    int bw = screenW * 2 / 3;
    int bh = screenH / 3;
    int bx = cx - bw / 2;
    int by = cy - bh / 2;

    display->drawRect(bx - 8, by - 8, bw + 16, bh + 16, ui::BG_DARK);
    drawPanelCard(display, bx - 6, by - 6, bw + 12, bh + 12, edge, fillOuter, ui::CARD);

    drawTextCentered(display, cx, by + bh / 4 - 12, line1, edge, 4);
    drawTextCentered(display, cx, by + bh * 3 / 4 - 8, line2, ui::TEXT_MAIN, 2);
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
            while (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId != nodeId && kind == "STATUS" && value == "GAME_READY") {
                    std::cout << "[] GAME_READY  ->   \n";
                    return true;
                }
            }
        }

        elapsed += 100;
    }

    std::cout << "[]  ->    \n";
    return false;
}

bool CatchMindGame::waitForAllPlayersReadyAtStart() {
    if (display == nullptr || roleSock < 0) {
        return false;
    }


    static constexpr int TOTAL_PLAYERS = 3;

    bool myReady = false;
    std::vector<std::string> readyNodes;
    int lastDrawnCount = -1;  //      
    auto markReady = [&](const std::string &nid) -> bool {
        if (std::find(readyNodes.begin(), readyNodes.end(), nid) == readyNodes.end()) {
            readyNodes.push_back(nid);
            return true;  //  
        }
        return false;  //  
    };

    auto drawStartReadyIfChanged = [&]() {
        int cur = (int)readyNodes.size();
        if (cur == lastDrawnCount) return;  //    
        lastDrawnCount = cur;
        display->beginFrame();


        bool hasBg = display->drawPNG("/mnt/nfs/img/main_image.ppm", 0, 0, screenW, screenH);
        if (!hasBg) {
            drawBg(display, screenW, screenH, 0);
        }

        int cx = screenW / 2;


        std::string countText = "READY " + std::to_string((int)readyNodes.size()) + "/" + std::to_string(TOTAL_PLAYERS);

        int ty = screenH - 52;
        display->drawRect(cx - 90, ty - 4, 180, 42, 0x99000000);
        drawTextCentered(display, cx, ty, countText, myReady ? ui::TEXT_DIM : ui::ACCENT_WARM, 2);
        drawTextCentered(display, cx, ty + 24, myReady ? "WAITING..." : "TAP TO READY", ui::TEXT_DIM, 1);
        display->endFrame();
    };

    auto drawStartReady = [&]() { drawStartReadyIfChanged(); };


    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX = false;
        touchHasY = false;
    }



    if (roleSock >= 0) {
        char buffer[256];
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        while (recvfrom(roleSock, buffer, sizeof(buffer), MSG_DONTWAIT,
                       reinterpret_cast<sockaddr *>(&from), &fromLen) > 0) {
        }
    }

    auto lobbyEntryTime = std::chrono::steady_clock::now();

    auto sendReady = [&]() {
        myReady = true;
        markReady(nodeId);
        broadcastStatusMessage("LOBBY_READY");
        std::cout << "[]  READY (" << readyNodes.size() << "/" << TOTAL_PLAYERS << ")\n";
        lastDrawnCount = -1;  //   
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

                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - lobbyEntryTime).count();
                    if (elapsed < 200) continue;
                    bool isNew = markReady(senderNodeId);
                    if (isNew) {
                        std::cout << "[] " << senderNodeId << " READY ("
                                  << readyNodes.size() << "/" << TOTAL_PLAYERS << ")\n";
                        drawStartReady();
                    }
                }
            }
        }

        if (touchFd >= 0 && FD_ISSET(touchFd, &readfds)) {
            input_event ev{};
            while (read(touchFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {


                auto handleRelease = [&]() {
                    if (!myReady) {
                        std::cout << "[]   -> READY\n";
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


    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX = false;
        touchHasY = false;
    }

    if (!statusForOthers.empty()) {
        broadcastStatusMessage(statusForOthers);
    }

    display->beginFrame();

    drawBg(display, screenW, screenH, 0);

    display->drawRect(0, 0, screenW, 62, ui::BG_DARK);
    drawTextCentered(display, screenW / 2, 10, title + " SELECT", ui::ACCENT_WARM, 3);
    display->drawText(screenW - 190, 42, "TOUCH TO CHOOSE", ui::TEXT_DIM, 1);

    display->drawRect(0, screenH - 116, screenW, 116, ui::BG_DARK);
    display->drawText(screenW / 2 - 80, screenH - 100, "CONFIRM HERE", ui::TEXT_DIM, 1);

    const int menuTop = 72;

    const int menuBottom = screenH - 120;
    const int itemGap = 10;
    int itemH = (menuBottom - menuTop - ((int)options.size() - 1) * itemGap) / (int)options.size();
    itemH = std::max(44, itemH);

    for (size_t i = 0; i < options.size(); ++i) {
        int y = menuTop + (int)i * (itemH + itemGap);

        display->drawRect(24, y, screenW - 48, itemH, 0xcc0d0922);
        drawPanelCard(display, 24, y, screenW - 48, itemH,
                      i == 0 ? ui::ACCENT_WARM : ui::STROKE,
                      0x00000000, 0x00000000);

        std::string line = std::to_string((int)i + 1) + ".  " + toDisplayLabel(options[i]);
        display->drawText(52, y + std::max(10, (itemH / 2) - 10), line, ui::TEXT_MAIN, 2);

        std::cout << "  " << (i + 1) << ") " << options[i] << " [" << toDisplayLabel(options[i]) << "]\n";
    }
    display->endFrame();

    std::cout << "[] " << title << "   ...\n";

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

// Touch keypad layout

static const char *const KEYPAD_ROWS[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};

void CatchMindGame::drawChallengerKeypad(const std::string &currentInput) {
    if (display == nullptr) return;

    int drawAreaH = (screenH * 7) / 10;
    int inputAreaY = drawAreaH;
    int inputAreaH = screenH - drawAreaH;


    display->drawRect(0, inputAreaY, screenW, inputAreaH, ui::BG_MID);
    display->drawRect(0, inputAreaY, screenW, 2, ui::ACCENT);


    int dispH = 32;
    int dispY = inputAreaY + 4;
    drawPanelCard(display, 4, dispY, screenW - 130, dispH, ui::STROKE, ui::CARD, ui::CARD_ALT);
    if (!currentInput.empty()) {
        display->drawText(10, dispY + 8, currentInput, ui::TEXT_MAIN, 2);
    } else {
        display->drawText(10, dispY + 8, "...", ui::TEXT_DIM, 2);
    }


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
    std::cout << "[ANSWER ] P" << playerNum << " -> " << answer << "\n";
}


void CatchMindGame::drawChallengerAnswerScreen(int playerNum, const std::string &myInput, 
                                               const std::string &answer1, const std::string &answer2) {
    if (display == nullptr) return;

    int drawAreaH = (screenH * 7) / 10;
    int inputAreaY = drawAreaH;
    int inputAreaH = screenH - drawAreaH;
    int halfW = screenW / 2;


    drawPanelCard(display, canvasX - 5, canvasY - 5, canvasW + 10, canvasH + 10, ui::ACCENT, ui::CARD, 0x0b141c);



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


        drawPanelCard(display, 4, panelTop, halfW - 8, panelH2, ui::P1_ACCENT, ui::CARD, 0x102336);
        display->drawText(10, panelTop + 6, "P1:", ui::TEXT_MAIN, 2);
        if (!receivedAnswer1.empty())
            display->drawText(10, panelTop + 30, receivedAnswer1.substr(0, 18), ui::ACCENT_WARM, 2);
        else
            display->drawText(10, panelTop + 30, "waiting...", ui::TEXT_DIM, 2);


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
    std::cout << "[]   . : ok1/ng1/ok2/ng2  q=\n";

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;
        if (roleSock >= 0) { FD_SET(roleSock, &readfds); if (roleSock > maxfd) maxfd = roleSock; }
        if (touchFd >= 0)  { FD_SET(touchFd,  &readfds); if (touchFd  > maxfd) maxfd = touchFd; }

        timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 50000;
        select(maxfd + 1, &readfds, nullptr, nullptr, &tv);


        if (roleSock >= 0 && FD_ISSET(roleSock, &readfds)) {
            std::string kind, value, senderIp, senderNodeId;
            while (receiveControlMessage(kind, value, senderIp, senderNodeId)) {
                if (senderNodeId == nodeId) continue;
                if (kind == "ANSWER") {

                    auto sep = value.find(':');
                    if (sep != std::string::npos) {
                        int pn = std::stoi(value.substr(0, sep));
                        std::string ans = value.substr(sep + 1);
                        if (pn == 1) { receivedAnswer1 = ans; answerReceived1 = true; }
                        if (pn == 2) { receivedAnswer2 = ans; answerReceived2 = true; }
                        std::cout << "[] P" << pn << "  : " << ans << "\n";
                        drawJudge();
                    }
                }
            }
        }


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

// ---------------------------------------------------------------------------

void CatchMindGame::broadcastScoreDelta(const std::string &targetNodeId, int delta) {

    gameScores[targetNodeId] += delta;
    std::cout << "[] " << targetNodeId << " delta=" << delta
              << " total=" << gameScores[targetNodeId] << "\n";
}

void CatchMindGame::drawTimerGauge(int remainSec, int totalSec, const std::string &mood) {
    if (display == nullptr) return;
    const int barH = TIMER_BAR_H;
    const int barY = topH;
    const int barW = screenW;

    // 1. Redraw timer bar background
    display->drawRect(0, barY, barW, barH, 0x0a1810);

    // 2. Fill elapsed time (100->0 decreasing)
    int safeRemain = std::max(0, remainSec);
    int remainW = totalSec > 0 ? (safeRemain * barW) / totalSec : 0;
    remainW = std::max(0, std::min(barW, remainW));

    unsigned int fillColor;
    if      (remainSec > 30) fillColor = 0x00cc55;   // 
    else if (remainSec > 10) fillColor = 0xff9900;   // 
    else                     fillColor = 0xff3030;   // 

    if (remainW > 0)
        display->drawRect(0, barY + 1, remainW, barH - 2, fillColor);


    char buf[12];
    int mm = remainSec / 60, ss = remainSec % 60;
    snprintf(buf, sizeof(buf), "%d:%02d", mm, ss);
    int tLen = (int)strlen(buf) * 6;
    display->drawText(barW - tLen - 8, barY + (barH - 8) / 2, buf, ui::TEXT_DIM, 1);

    // 3. Character image (inside bar to avoid ghost)
    const int cSize = std::max(8, barH - 4);
    int charCX = remainW;
    if (charCX < cSize / 2 + 2) charCX = cSize / 2 + 2;
    if (charCX > barW - cSize / 2 - 2) charCX = barW - cSize / 2 - 2;

    int charX = charCX - cSize / 2;
    int charY = barY + (barH - cSize) / 2;

    std::string charImgPath = "/mnt/nfs/img/player" + std::to_string(myBoardNum) + "/" + mood + ".ppm";
    if (!display->drawPNG(charImgPath, charX, charY, cSize, cSize))
        display->drawPNG("/mnt/nfs/img/character.ppm", charX, charY, cSize, cSize);
}

void CatchMindGame::showCorrectScreen(int winnerBoardNum, bool iAmWinner, int durationMs) {
    if (display == nullptr) return;
    display->beginFrame();
    display->clearScreen(ui::BG_DARK);

    int cx = screenW / 2;
    int cy = screenH / 2;

    int bw = std::min(screenW - 40, 520);
    int bh = std::min(screenH - 40, 320);
    int bx = cx - bw / 2;
    int by = cy - bh / 2;
    drawPanelCard(display, bx, by, bw, bh, ui::OK, ui::CARD, 0x0b1c10);


    int charSize = std::min(bh / 2, bw / 3);
    int charX = cx - charSize / 2;
    int charY = by + 12;
    std::string charPath = "/mnt/nfs/img/player" + std::to_string(winnerBoardNum) + "/smile.ppm";
    if (!display->drawPNG(charPath, charX, charY, charSize, charSize))
        display->drawPNG("/mnt/nfs/img/character.ppm", charX, charY, charSize, charSize);

    // "PLAYER X CORRECT!"
    std::string line1 = "PLAYER " + std::to_string(winnerBoardNum) + " CORRECT!";
    drawTextCentered(display, cx, charY + charSize + 14, line1, ui::OK, 3);


    std::string line2 = iAmWinner ? "YOU ARE NEXT DRAWER"
                                  : ("PLAYER " + std::to_string(winnerBoardNum) + " WINS!");
    drawTextCentered(display, cx, charY + charSize + 48, line2, ui::TEXT_MAIN, 2);

    display->endFrame();
    usleep(durationMs * 1000);
}

void CatchMindGame::showFinalScores() {
    if (display == nullptr) return;

    // Step 1: Submit scores to aggregator (PLAYER1) and redistribute snapshot

    const int SCORE_WAIT_MS = 10000;
    const std::string scoreCollectorNodeId = "PLAYER1";
    const std::vector<std::string> expectedPlayers = {"PLAYER1", "PLAYER2", "PLAYER3"};

    if (roleSock >= 0) {
        int myScore = gameScores.count(nodeId) ? gameScores[nodeId] : 0;
        gameScores[nodeId] = myScore;

        const bool isCollector = (nodeId == scoreCollectorNodeId);
        std::set<std::string> submittedPlayers;
        submittedPlayers.insert(nodeId);
        bool snapshotReady = false;

        auto parseScorePair = [&](const std::string &token, std::string &pid, int &score) -> bool {
            auto colon = token.find(':');
            if (colon == std::string::npos) return false;
            pid = token.substr(0, colon);
            try {
                score = std::stoi(token.substr(colon + 1));
            } catch (...) {
                return false;
            }
            return !pid.empty();
        };

        auto applySnapshot = [&](const std::string &snapshot) -> int {
            int applied = 0;
            std::stringstream ss(snapshot);
            std::string token;
            while (std::getline(ss, token, ',')) {
                std::string pid;
                int score = 0;
                if (!parseScorePair(token, pid, score)) continue;
                gameScores[pid] = score;
                applied++;
            }
            return applied;
        };

        auto buildSnapshot = [&]() -> std::string {
            std::string out;
            for (size_t i = 0; i < expectedPlayers.size(); ++i) {
                const std::string &pid = expectedPlayers[i];
                int score = gameScores.count(pid) ? gameScores[pid] : 0;
                if (!out.empty()) out += ",";
                out += pid + ":" + std::to_string(score);
            }
            return out;
        };

        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port   = htons(37031);
        to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        std::string submitValue = nodeId + ":" + std::to_string(myScore);
        std::string submitPayload = "CM|" + nodeId + "|FINAL_SCORE_SUBMIT|" + submitValue;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(SCORE_WAIT_MS);


        display->beginFrame();
        drawBg(display, screenW, screenH, 2);
        int cx = screenW / 2;
        if (isCollector) {
            drawTextCentered(display, cx, screenH / 2 - 30, "COLLECTING SCORES", ui::ACCENT_WARM, 2);
            drawTextCentered(display, cx, screenH / 2 + 10, "Collector: P1", ui::TEXT_DIM, 1);
        } else {
            drawTextCentered(display, cx, screenH / 2 - 30, "SUBMITTING SCORE", ui::ACCENT_WARM, 2);
            drawTextCentered(display, cx, screenH / 2 + 10, "Waiting for final snapshot", ui::TEXT_DIM, 1);
        }
        display->endFrame();

        auto lastRetry = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() < deadline && !snapshotReady) {
            auto now = std::chrono::steady_clock::now();


            if (!isCollector &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRetry).count() >= 400) {
                lastRetry = now;
                sendto(roleSock, submitPayload.c_str(), submitPayload.size(), 0,
                       reinterpret_cast<sockaddr *>(&to), sizeof(to));
            }


            if (isCollector && (int)submittedPlayers.size() >= (int)expectedPlayers.size()) {
                std::string snapshot = buildSnapshot();
                std::string syncPayload = "CM|" + nodeId + "|FINAL_SCORE_SYNC|" + snapshot;
                for (int i = 0; i < 5; ++i) {
                    sendto(roleSock, syncPayload.c_str(), syncPayload.size(), 0,
                           reinterpret_cast<sockaddr *>(&to), sizeof(to));
                    usleep(20000);
                }
                applySnapshot(snapshot);
                snapshotReady = true;
                break;
            }

            fd_set fds; FD_ZERO(&fds); FD_SET(roleSock, &fds);
            timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 50000;
            if (select(roleSock + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;

            std::string kind, value, sip, snid;
            while (receiveControlMessage(kind, value, sip, snid)) {
                if (snid == nodeId) continue;

                if (kind == "FINAL_SCORE_SYNC") {
                    int applied = applySnapshot(value);
                    if (applied > 0) {
                        snapshotReady = true;
                    }
                } else if (kind == "FINAL_SCORE_SUBMIT" || kind == "FINAL_SCORE") {

                    if (isCollector) {
                        std::string pid;
                        int score = 0;
                        if (parseScorePair(value, pid, score)) {
                            gameScores[pid] = score;
                            submittedPlayers.insert(pid);

                            display->beginFrame();
                            drawBg(display, screenW, screenH, 2);
                            drawTextCentered(display, cx, screenH / 2 - 30, "COLLECTING SCORES", ui::ACCENT_WARM, 2);
                            std::string prog = std::to_string((int)submittedPlayers.size()) + "/" +
                                               std::to_string((int)expectedPlayers.size()) + " received";
                            drawTextCentered(display, cx, screenH / 2 + 10, prog, ui::TEXT_DIM, 1);
                            display->endFrame();
                        }
                    }
                }
            }
        }

        if (!snapshotReady && isCollector) {

            std::string snapshot = buildSnapshot();
            std::string syncPayload = "CM|" + nodeId + "|FINAL_SCORE_SYNC|" + snapshot;
            for (int i = 0; i < 5; ++i) {
                sendto(roleSock, syncPayload.c_str(), syncPayload.size(), 0,
                       reinterpret_cast<sockaddr *>(&to), sizeof(to));
                usleep(20000);
            }
            applySnapshot(snapshot);
            snapshotReady = true;
            std::cout << "[]   ->    \n";
        } else if (!snapshotReady) {
            std::cout << "[]    ->   \n";
        }

        // Step 2: ALL_READY barrier - 3 boards enter results screen simultaneously

        const int OTHERS = 2;
        std::string readyPayload = "CM|" + nodeId + "|FINAL_READY|1";
        for (int i = 0; i < 3; ++i) {
            sendto(roleSock, readyPayload.c_str(), readyPayload.size(), 0,
                   reinterpret_cast<sockaddr *>(&to), sizeof(to));
            usleep(20000);
        }

        std::set<std::string> readyFrom;
        auto barrierDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
        auto lastRR = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() < barrierDeadline &&
               (int)readyFrom.size() < OTHERS) {

            auto now2 = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now2 - lastRR).count() >= 800) {
                lastRR = now2;
                sendto(roleSock, readyPayload.c_str(), readyPayload.size(), 0,
                       reinterpret_cast<sockaddr *>(&to), sizeof(to));
            }
            fd_set fds2; FD_ZERO(&fds2); FD_SET(roleSock, &fds2);
            timeval tv2{}; tv2.tv_sec = 0; tv2.tv_usec = 30000;
            if (select(roleSock + 1, &fds2, nullptr, nullptr, &tv2) <= 0) continue;
            std::string kind, value, sip, snid;
            while (receiveControlMessage(kind, value, sip, snid)) {
                if (snid == nodeId) continue;

                if (kind == "FINAL_SCORE_SYNC") {
                    applySnapshot(value);
                } else if (kind == "FINAL_SCORE") {
                    std::string pid;
                    int score = 0;
                    if (parseScorePair(value, pid, score)) {
                        gameScores[pid] = score;
                    }
                }
                if (kind == "FINAL_READY") {
                    readyFrom.insert(snid);
                    std::cout << "[] " << snid << " FINAL_READY (" << readyFrom.size() << "/" << OTHERS << ")\n";
                }
            }
        }
        std::cout << "[]  ->   \n";
    }

    // Render results screen

    std::vector<std::pair<int, std::string>> sorted;
    for (auto &kv : gameScores)
        sorted.push_back({kv.second, kv.first});
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });

    display->beginFrame();

    drawBg(display, screenW, screenH, 2);

    int cx = screenW / 2;


    display->drawRect(cx - 240, 10, 480, 72, ui::BG_DARK);
    drawPanelCard(display, cx - 236, 10, 472, 72, ui::NG, 0x2a0b0b, 0x1a0808);
    drawTextCentered(display, cx, 18, "GAME OVER", ui::NG, 4);
    drawTextCentered(display, cx, 60, "FINAL SCORES", ui::TEXT_DIM, 1);

    int startY = 90;
    int rowH = (screenH - startY - 60) / 3;
    rowH = std::max(56, std::min(rowH, 72));

    // Compute actual ranks (ties share the same rank)
    // e.g. 7 7 5 → rank 1 1 3,  7 5 5 → rank 1 2 2,  all equal → rank 1 1 1
    std::vector<int> ranks(sorted.size());
    for (int i = 0; i < (int)sorted.size(); ++i) {
        int r = 1;
        for (int j = 0; j < i; ++j) {
            if (sorted[j].first > sorted[i].first) ++r;
        }
        ranks[i] = r;
    }

    const unsigned int rankColor_all[] = {0xFFD700, 0xC0C0C0, 0xCD7F32, 0xAAAAAA};

    for (int i = 0; i < (int)sorted.size() && i < 3; ++i) {
        int rank = ranks[i];                          // 1-based actual rank
        int colorIdx = std::min(rank - 1, 3);         // clamp to array bounds
        unsigned int rc = rankColor_all[colorIdx];
        std::string rankLabel = "#" + std::to_string(rank);

        int y = startY + i * (rowH + 8);

        display->drawRect(cx - 240, y, 480, rowH, ui::BG_MID);
        drawPanelCard(display, cx - 240, y, 480, rowH, rc, ui::CARD, ui::BG_MID);

        display->drawText(cx - 228, y + rowH / 2 - 12, rankLabel.c_str(), rc, 2);

        int playerNum = 0;
        const std::string &id = sorted[i].second;
        if (id.rfind("PLAYER", 0) == 0) {
            try { playerNum = std::stoi(id.substr(6)); } catch (...) {}
        }

        if (playerNum >= 1 && playerNum <= 3) {
            // mood: 1st place smile, 2nd normal, 3rd+ cry
            const char *moodStr = (rank == 1) ? "smile" : (rank == 2) ? "normal" : "cry";
            int charSz = rowH - 6;
            int charX2 = cx - 200;
            int charY2 = y + 3;
            std::string cpath = "/mnt/nfs/img/player" + std::to_string(playerNum) + "/" + moodStr + ".ppm";
            if (!display->drawPNG(cpath, charX2, charY2, charSz, charSz))
                display->drawPNG("/mnt/nfs/img/character.ppm", charX2, charY2, charSz, charSz);
        }

        std::string plabel;
        if (playerNum >= 1 && playerNum <= 3) {
            plabel = "P" + std::to_string(playerNum);
        } else {
            plabel = id.substr(0, 8);
        }
        if (id == nodeId) plabel += " (YOU)";

        drawTextCentered(display, cx - 30, y + rowH / 2 - 12, plabel,
                         (id == nodeId) ? ui::ACCENT_WARM : ui::TEXT_MAIN, 2);

        std::string scoreStr = std::to_string(sorted[i].first) + " pts";
        drawTextCentered(display, cx + 160, y + rowH / 2 - 12, scoreStr, rc, 2);
    }

    display->drawRect(cx - 200, screenH - 32, 400, 24, ui::BG_DARK);
    drawTextCentered(display, cx, screenH - 28, "TAP or press any key to exit", ui::TEXT_DIM, 1);
    display->endFrame();


    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
    }


    while (true) {
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

    // Drain any residual touch events so they don't fire in the next screen
    if (touchFd >= 0) {
        input_event tmp{};
        while (read(touchFd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp)) {}
        touchPressed = false;
        touchHasX    = false;
        touchHasY    = false;
    }
    usleep(400000);  // 400 ms grace period before restart
}
