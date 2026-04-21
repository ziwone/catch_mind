# 캐치마인드 게임 사양서

## 구성
- 보드 3대: `192.168.10.3` (P1), `192.168.10.4` (P2), `192.168.10.5` (P3)
- 역할: 출제자 1명 + 도전자 2명
- 라운드 수: 3라운드 (`MAX_ROUNDS = 3`)
- 라운드 제한 시간: 60초 (`ROUND_TIMEOUT_SEC = 60`)
- 단어 은행: `include/wordbank.h` (7개 카테고리, 카테고리당 약 40개)
  - `ANIMAL` / `FRUIT` / `FOOD` / `OBJECT` / `NATURE` / `SPORT` / `PLACE`

---

## 게임 흐름

### 1. 역할 선택 화면
- 3보드가 동시에 역할 선택 화면 진입 (전원 `STATUS|READY` 합의 후)
- 화면 좌측 터치 → 출제자 선택, 우측 터치 → 도전자 선택
- 출제자를 선택한 보드가 `ROLE|DRAWER` 브로드캐스트
- 나머지 두 보드는 `STATUS|CHALLENGER_JOIN` 전송 → P1/P2 슬롯 자동 배정
  - 슬롯 배정: 보드 번호와 출제자 번호 조합으로 결정 (`getChallengerSlotByDrawer`)
- 직전 라운드 정답자가 있으면 자동으로 다음 라운드 출제자로 배정

### 2. 카테고리 / 주제어 선택 (출제자만)
- 전체 카테고리 중 **랜덤 3개** 표시 → 출제자 터치로 선택 → 확인 다이얼로그
- 선택된 카테고리에서 **랜덤 4개** 단어 표시 → 출제자 터치로 주제어 선택
- 선택 완료 시 `STATUS|DRAWING_START` 4회 반복 전송 (150ms 간격) → 싱크 보장
- `STATUS|GAME_READY` 전송 후 출제자 라운드 화면 진입
- 도전자 보드는 `DRAWING_START` 수신 후 전환 화면 표시 → `GAME_READY` 대기(최대 4초) → 라운드 진입

### 3. 라운드 진행
#### 화면 레이아웃 (출제자 / 도전자 공통)
```
┌────────────┬─────────────────────────────┐
│  INFO 패널 │                             │
│  (150px)   │       드로잉 캔버스          │
│  BRDn      │                             │
│  카테고리  │                             │
│  타이머    ├─────────────────────────────┤
│  캐릭터    │      타이머 게이지 바 (28px) │
├────────────┴──────────┬──────────────────┤
│   도전자 P1 답변 패널  │ 도전자 P2 답변 패널│
└───────────────────────┴──────────────────┘
```

#### 출제자 동작
- 터치 드로잉 → `DRAW|x,y` / `DRAW|UP` 브로드캐스트
- 캔버스 지우기 → `CLEAR` 브로드캐스트
- 도전자 답변 수신 → OK / NG 버튼으로 판정
  - **OK** → `STATUS|CORRECT_P{N}` 브로드캐스트, 라운드 종료
  - **NG** → `STATUS|A_CLEAR_P{N}` + `STATUS|RETRY_P{N}` 브로드캐스트, 도전자 재도전

#### 도전자 동작
- 출제자 드로잉 실시간 수신 및 화면 반영
- 터치 키패드로 답 입력 후 제출
- NG 수신 시: 답변 패널 초기화, 캐릭터 3초 cry 표정 후 normal 복귀
- 정답 수신 시: 캐릭터 smile 표정, 정답 화면 표시

#### 힌트
- 경과 시간 30초 도달 시 출제자가 `STATUS|HINT#{카테고리}` 브로드캐스트
- 도전자 화면 왼쪽 INFO 패널에 카테고리명을 크게(scale 3) 표시

#### 시간 초과
- 60초 경과 시 출제자가 `STATUS|WRONG_ALL#{정답}` 브로드캐스트
- 전체 화면에 정답 공개, 점수 없음, 역할 선택으로 복귀

### 4. 캐릭터 표정 시스템
| 상황 | 출제자 | 도전자 |
|------|--------|--------|
| 남은 시간 > 30초 | smile | normal |
| 남은 시간 10~30초 | normal | normal |
| 남은 시간 < 10초 | cry | normal |
| NG 판정 직후 (3초) | — | cry |
| 정답 | — | smile |

캐릭터 이미지: `/mnt/nfs/img/player{N}/{mood}.ppm`

### 5. 점수 및 최종 결과

#### 점수 규칙
| 상황 | 정답자 | 출제자 |
|------|--------|--------|
| 정답 (남은 시간 30초 이상) | **+3점** | **+2점** |
| 정답 (남은 시간 30초 미만) | **+2점** | **+1점** |
| 시간 초과 (정답 없음) | — | **-1점** |

- `FINAL_SCORE_SUBMIT` 메시지로 전체 보드 점수 집계 (PLAYER1이 aggregator 역할)
- 전체 라운드 종료 후 `STATUS|GAME_OVER` 브로드캐스트 → 최종 점수판 표시

---

## 네트워크 프로토콜

### 전송 방식
- UDP 브로드캐스트, 포트 `37031`
- 메시지 형식: `CM|<nodeId>|<kind>|<value>`
- 중요 메시지는 3회 재전송 (150ms 간격)

### 주요 메시지 목록
| kind | value | 방향 | 설명 |
|------|-------|------|------|
| `ROLE` | `DRAWER` | 출제자 → 전체 | 출제자 선택 알림 |
| `STATUS` | `CHALLENGER_JOIN` | 도전자 → 전체 | 도전자 참가 및 슬롯 협상 |
| `STATUS` | `DRAWING_START` | 출제자 → 전체 | 라운드 시작 신호 (4회) |
| `STATUS` | `GAME_READY` | 출제자 → 전체 | 출제자 라운드 화면 진입 완료 |
| `STATUS` | `DRAWING_ACTIVE` | 출제자 → 전체 | 첫 스트로크 시작 알림 |
| `DRAW` | `x,y` / `UP` | 출제자 → 전체 | 드로잉 좌표 스트림 |
| `CLEAR` | — | 출제자 → 전체 | 캔버스 초기화 |
| `STATUS` | `HINT#{카테고리}` | 출제자 → 전체 | 30초 힌트 |
| `STATUS` | `A_CLEAR_P{N}` | 출제자 → 전체 | P{N} 답변 패널 초기화 |
| `STATUS` | `RETRY_P{N}` | 출제자 → 전체 | P{N} 재도전 요청 |
| `STATUS` | `CORRECT_P{N}` | 출제자 → 전체 | P{N} 정답 판정 |
| `STATUS` | `WRONG_ALL#{정답}` | 출제자 → 전체 | 시간 초과, 정답 공개 |
| `STATUS` | `GAME_OVER` | 출제자 → 전체 | 전체 라운드 종료 |
| `FINAL_SCORE_SUBMIT` | `P1:n,P2:n,P3:n` | 각 보드 → 전체 | 최종 점수 제출 |
| `A_DRAW` | `pn,x,y` / `pn,UP` | 도전자 → 전체 | 도전자 답변 입력 스트림 |

---

## 소스 구조
```
include/
  game.h          게임 클래스 선언
  wordbank.h      카테고리/단어 데이터 (직접 편집 가능)
  display.h       프레임버퍼 디스플레이 API
  bgm.h           배경음악 제어
src/
  main.cpp        진입점, 보드 번호 인자 처리
  game.cpp        전체 게임 로직
  display.cpp     프레임버퍼 렌더링 구현
  bgm.cpp         BGM 재생 구현 (aplay)
  fb_server.c     프레임버퍼 서버 (저수준 초기화)
img/
  main_image.png  타이틀 이미지
  character.ppm   기본 캐릭터
  player{1-3}/    보드별 캐릭터 (smile/normal/cry)
bgm/              배경음악 파일
```

---

## 사용 기술 정리

### 언어 및 표준
| 항목 | 내용 |
|------|------|
| 언어 | C++17, C (fb_server) |
| 빌드 | aarch64-linux-gnu-g++ (크로스컴파일), Makefile |
| 타깃 플랫폼 | Linux aarch64 (Cortex-A 계열 SBC) |

### 디스플레이
| 항목 | 내용 |
|------|------|
| 출력 장치 | Linux Framebuffer (`/dev/fb0`) |
| API | `ioctl(FBIOGET_VSCREENINFO)`, `mmap` |
| 더블 버퍼링 | 소프트웨어 섀도우 버퍼 + `memcpy` 플립 |
| 폰트 렌더링 | 5×7 비트맵 폰트 직접 구현 (외부 라이브러리 없음) |
| 이미지 로딩 | PPM(P6) 바이너리 포맷 파싱 (직접 구현) |
| VSync | `FBIO_WAITFORVSYNC` ioctl |
| 커서 숨기기 | `/dev/tty` `KDSETMODE KD_GRAPHICS` |

### 터치 입력
| 항목 | 내용 |
|------|------|
| 입력 장치 | Linux Input Subsystem (`/dev/input/event*`) |
| 이벤트 타입 | `ABS_MT_POSITION_X/Y`, `BTN_TOUCH`, `SYN_REPORT` |
| 헤더 | `<linux/input.h>` |
| 좌표 보정 | Raw ADC 값 → 화면 픽셀 좌표 선형 매핑 |

### 네트워크
| 항목 | 내용 |
|------|------|
| 프로토콜 | UDP 소켓, 브로드캐스트 (`SO_BROADCAST`) |
| 포트 | 37031 |
| 다중화 | `select()` 기반 논블로킹 I/O |
| IP 감지 | `getifaddrs()` + `AF_INET` 루프백 제외 |
| 재전송 | 중요 메시지 3~4회 반복 (150ms 간격) |
| 동기화 배리어 | `DRAWING_START` × 4 + `waitForGameReady()` |

### 멀티스레딩
| 항목 | 내용 |
|------|------|
| 라이브러리 | POSIX Threads (`pthread`) |
| 용도 | FB 모니터 HTTP 서버를 백그라운드 스레드로 분리 |

### 오디오
| 항목 | 내용 |
|------|------|
| 재생 방식 | `fork` + `execl`로 `aplay` 프로세스 실행 |
| 볼륨 제어 | `amixer cset numid=1` |
| 카드 선택 | `/mnt/nfs/alsa_card` 파일로 런타임 설정 |
| 포맷 | WAV (PCM) |

### 시간 처리
| 항목 | 내용 |
|------|------|
| 타이머 | `std::chrono::steady_clock` |
| 대기 | `usleep`, `select` 타임아웃 |

### FB 모니터 서버
| 항목 | 내용 |
|------|------|
| 구현 | C 단일 파일 (`fb_server.c`) |
| 기능 | `/dev/fb0`를 PPM으로 캡처 → HTTP GET으로 브라우저 전송 |
| 포트 | 8080 |
| 용도 | 보드 화면 원격 확인 (개발/디버깅) |
