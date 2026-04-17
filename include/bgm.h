#ifndef BGM_H
#define BGM_H

#include <atomic>
#include <string>
#include <thread>
#include <sys/types.h>

class BgmPlayer {
public:
    BgmPlayer();
    ~BgmPlayer();

    // 지정한 WAV 파일을 aplay로 백그라운드에서 반복 재생
    bool play(const std::string &filePath);
    // 지정한 WAV 파일을 한 번만 재생 (논블로킹)
    void playOnce(const std::string &filePath);
    // 재생 중지 및 스레드 종료 대기
    void stop();
    // 볼륨 설정 (0~100)
    void setVolume(int percent);

private:
    std::thread        bgmThread;
    std::atomic<bool>  running{false};
    std::atomic<pid_t> childPid{-1};
    std::string        filePath;

    void threadFunc();
};

#endif // BGM_H
