#include "bgm.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// 테스트에서 검증된 경로 그대로 사용
static const char *ALSA_SH    = "/mnt/nfs/alsa.sh";
static const char *APLAY_BIN  = "/mnt/nfs/aplay";
static const char *AMIXER_BIN = "/mnt/nfs/amixer";
static const char *AUDIO_DEV  = "hw:3,0";

BgmPlayer::BgmPlayer()  {}
BgmPlayer::~BgmPlayer() { stop(); }

void BgmPlayer::setVolume(int percent) {
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    // ". alsa.sh && amixer -c 3 cset numid=1 50%" 형태로 실행
    const std::string cmd =
        std::string(". ") + ALSA_SH +
        " && " + AMIXER_BIN +
        " -c 3 cset numid=1 " + std::to_string(percent) + "%";

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
    // alsa.sh를 source한 뒤 aplay 실행 — 테스트와 동일한 환경
    const std::string cmd =
        std::string(". ") + ALSA_SH +
        " && " + APLAY_BIN +
        " -D" + AUDIO_DEV +
        " " + filePath;

    while (running) {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "[BGM] fork 실패: " << strerror(errno) << "\n";
            break;
        }

        if (pid == 0) {
            // 자식: sh -c "source alsa.sh && aplay -Dhw:0,0 <file>"
            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            std::cerr << "[BGM] execl 실패: " << strerror(errno) << "\n";
            _exit(1);
        }

        // 부모: aplay 종료 대기 (곡 끝나면 running이 true인 동안 반복)
        childPid.store(pid);
        int status = 0;
        waitpid(pid, &status, 0);
        childPid.store(-1);
    }
}
