#ifndef GAME_H
#define GAME_H

#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include "display.h"

class CatchMindGame {
private:
    int round = 0;
    bool isDrawing = false;
    Display *display = nullptr;

    // 캔버스/레이아웃
    int screenW = 0;
    int screenH = 0;
    int topH = 0;
    int bottomY = 0;
    int panelH = 0;

    int canvasX = 20;
    int canvasY = 20;
    int canvasW = 0;
    int canvasH = 0;

    // 브러시
    int cursorX = 0;
    int cursorY = 0;
    bool penDown = true;
    unsigned int brushColor = 0xffffff;

    // 터치 입력
    int touchFd = -1;
    int touchMaxX = 0;
    int touchMaxY = 0;
    int touchRawX = 0;
    int touchRawY = 0;
    bool touchPressed = false;
    bool touchHasX = false;
    bool touchHasY = false;

    // 게임 상태
    bool isDrawerRole = true;
    int roleSock = -1;
    std::string nodeId;
    std::string drawerIp;
    std::string currentCategory;
    std::string targetWord;
    std::string currentDrawerNodeId;
    std::vector<std::string> offeredWords;
    std::string player1LatestAnswer;
    std::string player2LatestAnswer;

    std::mt19937 rng;
    std::unordered_map<std::string, std::vector<std::string>> wordBank;

    // 흐름
    bool roleSelection();
    bool selectCategoryAndWord();
    void runSingleBoardRound();
    void runChallengerStandby();
    void runChallengerLiveRound();

    // 화면
    void drawGameLayout();
    void drawStatus();
    void drawBrushDot(int x, int y);
    void resetCanvas();
    void paintAnswerPanel(int playerIndex, unsigned int color);
    bool initTouchInput();
    void closeTouchInput();
    void processTouchEvents();
    bool mapTouchToScreen(int rawX, int rawY, int &x, int &y) const;
    bool initRoleSocket();
    void closeRoleSocket();
    void broadcastDrawerSelected();
    void broadcastStatusMessage(const std::string &status);
    void broadcastDrawPoint(int x, int y, unsigned int color);
    void broadcastCanvasClear();
    bool receiveControlMessage(std::string &kind, std::string &value, std::string &senderIp, std::string &senderNodeId);
    bool receiveDrawerSelected(std::string &senderIp);
    bool waitTouchReleasePoint(int &sx, int &sy, int timeoutMs);
    bool selectFromTouchMenu(const std::string &title,
                             const std::vector<std::string> &options,
                             int &selectedIndex,
                             unsigned int highlightColor,
                             const std::string &statusForOthers);

    // 유틸
    std::vector<std::string> pickRandomWords(const std::vector<std::string> &pool, int count);
    std::string normalizeText(const std::string &text);
    bool handleGuess(int playerIndex, const std::string &answer);
    bool handleDrawCommand(const std::string &cmd);
    void printRoundGuide();

public:
    CatchMindGame();
    ~CatchMindGame();

    bool initDisplay();
    void start();
    void stop();
};

#endif
