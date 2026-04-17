#include "bgm.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// 테스트에서 검증된 경로 그대로 사용
static const char *ALSA_SH    = "/mnt/nfs/alsa.sh";
static const char *APLAY_BIN  = "/mnt/nfs/aplay";
static const char *AMIXER_BIN = "/mnt/nfs/amixer";
// 카드 번호 플래그 파일: echo 3 > /nfsroot/alsa_card 처럼 숫자를 쓰면 해당 카드 사용
// 파일이 없으면 기본값 0 사용
static const char *ALSA_CARD_FILE = "/mnt/nfs/alsa_card";

static int readAlsaCard() {
    std::ifstream f(ALSA_CARD_FILE);
    int card = 0;  // 기본값
    if (f.is_open()) {
        f >> card;
    }
    return card;
}

BgmPlayer::BgmPlayer()  {}
BgmPlayer::~BgmPlayer() { stop(); }

void BgmPlayer::setVolume(int percent) {
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    int card = readAlsaCard();
    const std::string cmd =
        std::string(". ") + ALSA_SH +
        " && " + AMIXER_BIN +
        " -c " + std::to_string(card) +
        " cset numid=1 " + std::to_string(percent) + "%";

    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

bool BgmPlayer::play(const std::string &path) {
    stop();
    filePath = path;
    running  = true;
    bgmThread = std::thread(&BgmPlayer::threadFunc, this);
    return true;
}

void BgmPlayer::stop() {
    running = false;
    pid_t pid = childPid.load();
    if (pid > 0) {
        kill(pid, SIGTERM);
    }
    if (bgmThread.joinable()) {
        bgmThread.join();
    }
}

void BgmPlayer::threadFunc() {
    int card = readAlsaCard();
    std::string audioDev = "hw:" + std::to_string(card) + ",0";
    std::cout << "[BGM] ALSA card=" << card << " (" << ALSA_CARD_FILE << ")\n";

    const std::string cmd =
        std::string(". ") + ALSA_SH +
        " && " + APLAY_BIN +
        " -D" + audioDev +
        " " + filePath;

    while (running) {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "[BGM] fork 실패: " << strerror(errno) << "\n";
            break;
        }

        if (pid == 0) {
            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            std::cerr << "[BGM] execl 실패: " << strerror(errno) << "\n";
            _exit(1);
        }

        childPid.store(pid);
        int status = 0;
        waitpid(pid, &status, 0);
        childPid.store(-1);
    }
}
