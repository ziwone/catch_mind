# 캐치마인드 게임 사양서

## 구성
- 보드 3대: `192.168.10.3` (P1), `192.168.10.4` (P2), `192.168.10.5` (P3)
- 역할: 출제자 1명 + 도전자 2명
- 라운드 수: 5라운드 (`MAX_ROUNDS = 5`)
- 라운드 제한 시간: 60초 (`ROUND_TIMEOUT_SEC = 60`)
- 단어 은행: `include/wordbank.h` (7개 카테고리, 카테고리당 15~26개)
  - `ANIMAL`(24) / `FRUIT`(17) / `FOOD`(21) / `OBJECT`(24) / `NATURE`(15) / `SPORT`(17) / `PLACE`(19)

---

## 게임 흐름

### 1. 역할 선택 화면
- 3보드가 동시에 역할 선택 화면 진입 (전원 `STATUS|READY` 합의 후)
- 화면 좌측 터치 → 출제자 선택, 우측 터치 → 도전자 선택
- 출제자를 선택한 보드가 `ROLE|DRAWER` 브로드캐스트
- 나머지 두 보드는 `STATUS|CHALLENGER_JOIN` 전송 → P1/P2 슬롯 자동 배정
  - 슬롯 배정: 보드 번호와 출제자 번호 조합으로 결정 (`getChallengerSlotByDrawer`)
- **자동 출제자 배정**: 두 보드가 먼저 도전자를 선택(`CHALLENGER_JOIN` 2회 수신)하면, 나머지 보드가 자동으로 출제자가 됨 → 세 명 모두 도전자를 선택하는 상황 방지
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
- 도전자 답변 수신 → **터치(화면 OK/NG 버튼)** 또는 **물리 버튼(SW2/SW3)**으로 판정
  - **OK** → `STATUS|CORRECT_P{N}` 브로드캐스트, 라운드 종료
  - **NG** → `STATUS|A_CLEAR_P{N}` + `STATUS|RETRY_P{N}` 브로드캐스트, 도전자 재도전

#### 도전자 동작
- 출제자 드로잉 실시간 수신 및 화면 반영
- 터치 드로잉으로 답 작성 후 제출 (`A_DRAW|pn,x,y` / `A_DRAW|pn,UP` 브로드캐스트)
- **터치 버튼** 또는 **물리 버튼**으로 조작
  - **CLEAR** (터치 버튼 또는 SW2): 작성 중인 답변 지우기
  - **SUBMIT** (터치 버튼 또는 SW3): 답변 제출
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

#### 최종 점수판
- 3명의 점수를 내림차순 정렬 후 등수 표시
- **동점자는 같은 등수** 부여 (예: 7점 동점 2명 → 1등·1등·3등)
- 등수별 색상: 1등 금색, 2등 은색, 3등 동색
- 점수판 확인 후 자동으로 **역할 선택 화면부터 재시작** (프로세스 종료 없음)

---

## 네트워크 프로토콜

### 전송 방식
- 소켓: `SOCK_DGRAM` + `SO_BROADCAST` + `SO_REUSEADDR`, 포트 `37031`
- 메시지 형식: `CM|<nodeId>|<kind>|<value>` (`|` 구분자, `std::getline` 파싱)
- 발신자 IP: `recvfrom()`의 `sockaddr_in`에서 추출 → 슬롯 배정에 활용
- 자기 메시지 필터: `senderNodeId == nodeId` 조건으로 즉시 무시
- IP 감지: `getifaddrs()` + `AF_INET` 루프백(`127.x.x.x`) 제외로 보드 IP 자동 획득
- I/O 다중화: `select()` 기반 논블로킹 (타임아웃 5ms~200ms 상황별 조정)

### 재전송 횟수 (충돌/누락 대응)
| 메시지 | 재전송 횟수 | 간격 | 이유 |
|--------|------------|------|------|
| `DRAW` | 1회 | — | 실시간성 우선, 누락 허용 |
| `CLEAR`, `ROLE\|DRAWER`, `JUDGING_*` | 3회 | 50ms | 상태 동기화 |
| `ANSWER`, `A_CLEAR`, `RETRY_P{N}` | 3회 | 30ms | 판정 신뢰성 |
| `DRAWING_START` | 4회 | 150ms | 라운드 시작 싱크 배리어 |
| `FINAL_SCORE_SYNC` | 5회 | 20ms | 최종 점수 일관성 보장 |

### 충돌 방지 메커니즘

멀티플레이 구현 과정에서 보드 3대가 실시간으로 상태를 공유하면서 발생한 다양한 충돌 문제들과 그 해결 과정을 아래에 상세히 기록한다.

---

#### 문제 1: 도전자 답안 실시간 노출 문제

**초기 시도**: 출제자 드로잉처럼 도전자도 답 작성 중 획마다 `A_DRAW|pn,x,y` 패킷을 즉시 브로드캐스트했다.

**발생한 문제**:
- 상대 도전자가 내 답안이 완성되기 전에 중간 과정을 실시간으로 볼 수 있게 됨
- 두 도전자가 동시에 답을 그리면 서로의 획이 뒤섞이며 화면이 혼잡해짐
- 도전자 답안 패널 좌우에 각각 렌더링되어야 하는 획 구분이 불명확해짐

**해결책**: 도전자 답안은 **로컬에만 렌더링**하고 네트워크로는 전송하지 않다가, SUBMIT 버튼을 누르는 순간 `queuedInkPoints` 벡터에 쌓인 좌표 전체를 `A_POINT|{pn},nx,ny` 패킷으로 **일괄 전송**(`flushQueuedInk()`)한다. 상대방 및 출제자 화면에는 SUBMIT 이후에야 답안이 표시된다.

---

#### 문제 2: 두 도전자 동시 제출로 인한 출제자 UI 충돌

**발생한 문제**: 두 도전자가 거의 동시에 SUBMIT을 누르면 출제자가 두 `ANSWER` 메시지를 수 ms 간격으로 수신한다. 첫 번째 ANSWER를 처리해 OK/NG 판정 UI를 띄우는 도중에 두 번째 ANSWER가 들어오면서 화면이 덮어씌워지거나, OK/NG 버튼이 어떤 답안에 대한 것인지 불명확해지는 상황이 발생했다.

**해결책 (2단계)**:
1. **출제자 측**: `judgingActive` 플래그를 두어, 이미 판정 중(`true`)이면 새로 들어오는 `ANSWER` 패킷을 즉시 무시한다.
2. **도전자 측**: 출제자가 ANSWER를 수신하는 즉시 `STATUS|JUDGING_ACTIVE`를 3회 브로드캐스트한다. 도전자 보드는 이를 수신하면 `submitLocked = true`로 SUBMIT 버튼을 잠근다. 판정이 끝나면 `STATUS|JUDGING_END`를 보내 잠금을 해제한다.

이렇게 함으로써 판정 진행 중에는 어떤 도전자도 추가 제출이 불가능하다.

---

#### 문제 3: 패킷 잘림으로 인한 메시지 파싱 오류

**발생한 문제**: `A_POINT` 일괄 전송 시, 도전자가 긴 획을 그렸을 경우 누적된 좌표가 수십~수백 포인트에 달한다. 초기에는 이 좌표들을 하나의 UDP 패킷에 모두 담으려 했으나, UDP 페이로드 한계(일반적으로 약 65507 바이트, 실 네트워크 MTU 기준 훨씬 작음)를 초과하거나, `recv()` 수신 버퍼(`MSG_BUFSIZE`) 크기를 초과하면 메시지가 잘려서 수신됐다.

**증상**: 파싱 중 `|` 구분자 분리 후 값(`value`) 문자열 끝이 잘려 있어, 좌표 파싱 실패 또는 마지막 몇 포인트가 유실되는 현상.

**해결책**: 좌표를 묶음으로 전송하는 대신, 포인트 **1개당 1패킷**(`A_POINT|{pn},nx,ny`)으로 쪼개어 개별 전송하도록 변경했다. 패킷 수는 늘어나지만 개별 패킷은 수십 바이트 수준으로 잘림이 발생하지 않는다. SUBMIT → `flushQueuedInk()` 함수가 벡터를 순회하며 패킷을 하나씩 전송한다.

---

#### 문제 4: NG 판정 후 상대방 화면에 답안 잔상

**발생한 문제**: 출제자가 NG 판정을 내리면 해당 도전자 자신의 화면은 초기화되지만, 다른 도전자와 출제자 화면에는 해당 답안 획이 그대로 남아 있었다. 재도전 시 이전 답안 위에 새 획이 덧그려져 답안 패널이 지저분해졌다.

**해결책**: NG 판정 시 출제자가 `STATUS|A_CLEAR_P{N}`(3회/30ms)을 브로드캐스트하여 모든 보드가 해당 도전자 패널을 초기화하도록 했다. 이후 `STATUS|RETRY_P{N}`(3회/30ms)을 추가로 보내 재도전 상태로 전환한다.

---

#### 문제 5: SUBMIT 대기 루프에서 CLEAR 무시

**발생한 문제**: 도전자가 SUBMIT을 누른 후 판정 결과를 기다리는 `while(true)` 루프에 진입한다. 이 루프에서는 `A_POINT`, `JUDGING_ACTIVE`, `CORRECT_P`, `RETRY_P` 등만 처리하고 있었다. 이 사이에 출제자가 캔버스를 지우는 `CLEAR` 메시지를 보내도 도전자 화면에서는 처리되지 않아 출제자 캔버스가 여전히 채워진 채로 남아 있었다.

**해결책**: 대기 루프 내의 `kind` 분기에 `CLEAR` 핸들러를 추가하여, 대기 중에도 출제자의 캔버스 초기화 명령을 즉시 반영한다.

---

#### 문제 6: 점수 불일치

**발생한 문제**: 초기에는 각 보드가 수신한 `CORRECT_P{N}` 메시지를 보고 직접 모든 플레이어 점수를 계산하는 방식이었다. 그러나 네트워크 지연이나 패킷 누락으로 `CORRECT_P1` 메시지가 특정 보드에 늦게 도착하면, 라운드가 끝나는 시점에 보드 간 점수가 서로 달라지는 상황이 발생했다.

**해결책**: 각 보드는 **자기 자신의 점수만 직접 관리**한다. 라운드 종료 후 각 보드가 `FINAL_SCORE_SUBMIT|{nodeId}:{score}`를 400ms 간격으로 반복 전송하고, `PLAYER1` 보드가 3개 보드의 점수를 모두 수집한 뒤 `FINAL_SCORE_SYNC|PLAYER1:n,PLAYER2:n,PLAYER3:n` 스냅샷을 5회/20ms로 브로드캐스트한다. 모든 보드는 이 스냅샷을 기준으로 최종 점수판을 그린다. 이로써 점수 계산의 진원지를 단일화하여 불일치를 원천 차단했다.

---

#### 현재 적용 중인 충돌 방지 메커니즘 요약

| 상황 | 방어 로직 |
|------|----------|
| 판정 중 중복 제출 | `judgingActive` 플래그: 출제자가 이미 판정 중이면 추가 ANSWER 무시 |
| 도전자 이중 제출 | `submitLocked`: `JUDGING_ACTIVE` 수신 시 SUBMIT 버튼 잠금, `JUDGING_END` 후 해제 |
| 답안 노출 / 획 혼잡 | 도전자 답안 실시간 미전송 → SUBMIT 시 `queuedInkPoints` 일괄 플러시 |
| 패킷 잘림 | 좌표 1개당 1패킷(`A_POINT`)으로 분할 전송 |
| NG 후 답안 잔상 | `A_CLEAR_P{N}` 브로드캐스트로 전체 보드 패널 초기화 |
| SUBMIT 대기 중 CLEAR 무시 | 대기 루프 내 `kind == "CLEAR"` 핸들러 추가 |
| 점수 불일치 | 각 보드 자기 점수만 관리 → `FINAL_SCORE_SUBMIT` 집계 → PLAYER1 스냅샷 배포 |
| 도전자 메시지 위장 | `fromDrawer` 체크: DRAW/CLEAR/STATUS는 `currentDrawerNodeId` 소유자만 처리 |
| 자기 메시지 루프백 | `senderNodeId == nodeId` 즉시 무시 |
| 라운드 전환 중 잔류 패킷 | `ROUND_END` 수신 즉시 함수 `return`, 이후 패킷은 다음 라운드에서 무시 |
| 터치 이벤트 잔류 | 화면 전환 시 `read()` 루프로 버퍼 완전 소진 |

### 동기화 배리어
- **라운드 시작**: `DRAWING_START` × 4 → `waitForGameReady()` 최대 4초 대기
- **최종 점수**: `FINAL_SCORE_SUBMIT` 반복 전송 → PLAYER1이 집계 후 `FINAL_SCORE_SYNC` 스냅샷 브로드캐스트 → `FINAL_READY` 배리어로 3보드 동시 결과 화면 진입

### 주요 메시지 목록
| kind | value | 방향 | 재전송 | 설명 |
|------|-------|------|--------|------|
| `ROLE` | `DRAWER` | 출제자 → 전체 | 3회/50ms | 출제자 선택 알림 |
| `STATUS` | `CHALLENGER_JOIN` | 도전자 → 전체 | 1회 | 도전자 참가 및 슬롯 협상 |
| `STATUS` | `DRAWING_START` | 출제자 → 전체 | 4회/150ms | 라운드 시작 싱크 신호 |
| `STATUS` | `GAME_READY` | 출제자 → 전체 | 1회 | 출제자 라운드 화면 진입 완료 |
| `STATUS` | `DRAWING_ACTIVE` | 출제자 → 전체 | 1회 | 첫 스트로크 시작 알림 |
| `DRAW` | `nx,ny,color` | 출제자 → 전체 | 1회 | 드로잉 좌표 (0~999 정규화) |
| `CLEAR` | `1` | 출제자 → 전체 | 3회/50ms | 캔버스 초기화 |
| `STATUS` | `HINT#{카테고리}` | 출제자 → 전체 | 1회 | 30초 힌트 |
| `STATUS` | `JUDGING_ACTIVE` | 출제자 → 전체 | 3회/50ms | 판정 시작, 도전자 submit 잠금 |
| `STATUS` | `JUDGING_END` | 출제자 → 전체 | 3회/50ms | 판정 종료, 잠금 해제 |
| `STATUS` | `A_CLEAR_P{N}` | 출제자 → 전체 | 3회/30ms | P{N} 답변 패널 초기화 |
| `STATUS` | `RETRY_P{N}` | 출제자 → 전체 | 3회/30ms | P{N} 재도전 요청 |
| `STATUS` | `CORRECT_P{N}#BOARD{B}#EARLY/LATE` | 출제자 → 전체 | 1회 | 정답 판정 (보드번호, 타이밍 포함) |
| `STATUS` | `WRONG_ALL#{정답}` | 출제자 → 전체 | 1회 | 시간 초과, 정답 공개 |
| `STATUS` | `ROUND_END` | 출제자 → 전체 | 1회 | 라운드 종료 |
| `STATUS` | `GAME_OVER` | 출제자 → 전체 | 1회 | 전체 라운드 종료 |
| `ANSWER` | `{pn}:DRAWN` | 도전자 → 전체 | 3회/30ms | 답변 제출 |
| `A_POINT` | `{pn},nx,ny` | 도전자 → 전체 | 1회 | 답변 필기 좌표 (submit 시 일괄 전송) |
| `A_CLEAR` | `{pn}` | 도전자 → 전체 | 1회 | 도전자 자신의 답변 지우기 |
| `A_UP` | `{pn}` | 도전자 → 전체 | 1회 | 필기 스트로크 끝 |
| `FINAL_SCORE_SUBMIT` | `{nodeId}:{score}` | 각 보드 → 전체 | 400ms마다 반복 | 최종 점수 제출 |
| `FINAL_SCORE_SYNC` | `PLAYER1:n,PLAYER2:n,...` | PLAYER1 → 전체 | 5회/20ms | 전체 점수 스냅샷 |
| `FINAL_READY` | `1` | 각 보드 → 전체 | 800ms마다 반복 | 결과 화면 배리어 |

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

### 언어 및 빌드
| 항목 | 내용 |
|------|------|
| 언어 | C++17 (`game.cpp`, `display.cpp`, `bgm.cpp`), C (`fb_server.c`) |
| 빌드 | `aarch64-linux-gnu-g++` 크로스컴파일, `Makefile` |
| 타깃 | Linux aarch64 (Cortex-A 계열 SBC) |
| 배포 경로 | VirtualBox 공유폴더(`/media/sf_share/catch_mind`) → `rsync` → NFS export(`/nfsroot`) → 보드 NFS 마운트(`/mnt/nfs`) |

---

### 디스플레이 (`display.cpp`)

#### 초기화 흐름
1. `/dev/fb0` `O_RDWR` 열기
2. `ioctl(FBIOGET_VSCREENINFO)` → 해상도(`xres`/`yres`), bpp 획득
3. `ioctl(FBIOGET_FSCREENINFO)` → `line_length`, `smem_len` 획득
4. 하드웨어 더블버퍼 시도: `yres_virtual = yres*2`로 `FBIOPUT_VSCREENINFO` → 성공 시 페이지 플립 모드
5. 실패 시 소프트웨어 섀도우 버퍼(`malloc(pageSizeBytes)`) 사용
6. `mmap(PROT_READ|PROT_WRITE, MAP_SHARED)` 으로 프레임버퍼 메모리 직접 매핑
7. `/dev/tty0` 열고 `ioctl(KDSETMODE, KD_GRAPHICS)` + ANSI `\033[?25l` → 커서 완전 숨김

#### 더블 버퍼링 동작
- **하드웨어 페이지 플립**: `beginFrame()` 시 backPage를 frontPage에서 복사, `endFrame()` 시 `FBIO_WAITFORVSYNC` 후 `FBIOPAN_DISPLAY`로 yoffset 전환 → 찢김 없는 플리핑
- **소프트웨어 섀도우**: `beginFrame()` 시 `memcpy(shadow ← map)`, `endFrame()` 시 `memcpy(map ← shadow)` → 원자적 화면 갱신
- **프레임 밖 직접 드로잉**: `beginFrame()` 없이 `drawRect()` 호출 시 frontPage/backPage **양쪽**에 동시 기록 → 잔상 없이 즉시 반영

#### 픽셀 포맷 지원
- `bpp=32`: ARGB8888 직접 쓰기 (`*(uint32_t*) = color`)
- `bpp=16`: RGB565 변환 후 `*(uint16_t*)` 쓰기

#### 폰트 렌더링
- 5×7 비트맵 폰트 하드코딩 (A-Z, 0-9, `.:- /` 등)
- `scale` 파라미터로 정수 배율 확대
- 한글 불가 → 모든 UI 레이블은 ASCII

#### 이미지 로딩
- PPM P6 (바이너리) 포맷 직접 파싱
- 헤더(`P6\n width height\n maxval\n`) 읽은 후 RGB 픽셀 배열 → 화면 bpp에 맞게 변환 후 `drawRect`
- 파일 없으면 fallback: `/mnt/nfs/img/character.ppm`

---

### 터치 입력 (`game.cpp`)

#### 디바이스 탐색
- `/dev/input/event0` ~ `event15` 순서로 `O_RDONLY | O_NONBLOCK` 시도
- `ioctl(EVIOCGBIT)` 으로 `EV_ABS` 지원 확인 → 첫 번째 성공 디바이스 사용

#### 이벤트 처리
- `EV_ABS` + `ABS_X` / `ABS_MT_POSITION_X` → `touchRawX` 업데이트
- `EV_ABS` + `ABS_Y` / `ABS_MT_POSITION_Y` → `touchRawY` 업데이트
- `EV_ABS` + `ABS_MT_TRACKING_ID < 0` → 멀티터치 손 뗌 감지
- `EV_KEY` + `BTN_TOUCH value==0` → 싱글터치 손 뗌 감지 (두 경로 모두 처리)
- `EV_SYN` + `SYN_REPORT` → 좌표 확정 후 처리 (드로잉 중)

#### 좌표 보정
```
sx = (touchRawX - rawMinX) * screenW / (rawMaxX - rawMinX)
sy = (touchRawY - rawMinY) * screenH / (rawMaxY - rawMinY)
```
보정 상수는 보드별로 `mapTouchToScreen()` 내에 하드코딩

#### 디바운싱
- 릴리즈 중복 방지: 120ms 이내 + 6px 이내 재발생 릴리즈 무시 (`lastTapTime`/`lastTapX/Y`)
- 화면 전환 시 버퍼 드레인: `while(read(touchFd, &tmp, sizeof(tmp)) == sizeof(tmp)) {}` → ghost click 방지
- 스트로크 재연결 브리지: 손 뗐다가 120ms/48px 이내 재터치 시 끊긴 획 보간 연결

#### 드로잉 패킷 전송 방식
- 출제자: 터치 포인트마다 즉시 `broadcastDrawPoint()` → `DRAW|nx,ny,color` (0~999 정규화)
- 도전자: 포인트를 `queuedInkPoints` 벡터에 로컬 누적 → SUBMIT 시 `flushQueuedInk()`로 `A_POINT` 패킷 일괄 전송

---

### 물리 버튼 GPIO (`game.cpp`)

#### 초기화
- `/dev/gpiochip0`, `/dev/gpiochip1` 순서로 시도
- `ioctl(GPIO_GET_LINEEVENT_IOCTL, &gpioevent_request)` 로 이벤트 fd 획득
  - `handleflags = GPIOHANDLE_REQUEST_INPUT`
  - `eventflags = GPIOEVENT_REQUEST_FALLING_EDGE` (버튼 누름 = 하강 에지)
- 커널 ≥ 4.8, `CONFIG_GPIO_CDEV=y` 필요

#### 감지 루프
- 버튼 fd마다 백그라운드 `std::thread` 생성 (`gpioWatchThread`)
- 스레드 내부: `poll(POLLIN, 100ms)` → `gpioevent_data` 읽기 → `std::atomic<bool>` 플래그 `store(true)`
- 메인 루프에서 `flag.exchange(false)` 로 원자적 소비

#### 역할별 버튼 배정
| 버튼 | GPIO line | 출제자 | 도전자 |
|------|-----------|--------|--------|
| SW2 | line 17 | OK (정답) | CLEAR (지우기) |
| SW3 | line 18 | NG (오답) | SUBMIT (제출) |

#### 생명주기 관리
- RAII 구조체 `ChlGpioScope` (도전자) / 직접 `btnRunning` + `join` (출제자)
- 라운드 종료 시 `running.store(false)` → 스레드 자연 종료 → `join()` → `close(fd)`

---

### 오디오 (`bgm.cpp`)

#### 재생 방식
- `fork()` + `execl("/bin/sh", "sh", "-c", cmd)` → `/mnt/nfs/aplay` 실행
- ALSA 환경 초기화: `alsa.sh` 스크립트를 소싱(`. /mnt/nfs/alsa.sh`)한 후 실행
- 오디오 디바이스: `hw:<card>,0` (카드 번호는 런타임 결정)
- 카드 번호: `/mnt/nfs/alsa_card` 파일 읽기 → 없으면 기본값 0

#### 두 가지 재생 모드
| 함수 | 특성 | 용도 |
|------|------|------|
| `playOnce(path)` | 비차단 (`WNOHANG`), 자식 프로세스 방치 | 효과음 (정답/오답) |
| `play(path)` | 별도 스레드에서 `waitpid()` 대기 후 완료되면 루프 재시작 | BGM 반복 재생 |

#### 정지
- `stop()`: `childPid`에 `SIGTERM` 전송 + `running = false` + 스레드 `join()`

#### 볼륨 제어
- `fork()` + `execl("/bin/sh", "-c", "amixer -c <card> cset numid=1 <pct>%")`
- `waitpid()` 동기 대기 (볼륨은 블로킹 허용)

---

### FB 모니터 서버 (`fb_server.c`)

#### 개요
- 순수 C (libc + pthreads), 외부 라이브러리 없음
- TCP 포트 8080, `accept()` 마다 `pthread_create()` + `pthread_detach()`

#### HTTP 엔드포인트
| 경로 | 응답 | 설명 |
|------|------|------|
| `GET /` | HTML + JavaScript | 1초 간격 자동 갱신 페이지 |
| `GET /frame` | `image/bmp` | 현재 프레임버퍼 캡처 이미지 |

#### 캡처 파이프라인
1. `/sys/class/graphics/fb0/virtual_size`, `bits_per_pixel` 읽기 (sysfs)
2. `/dev/fb0` `read()` → 원본 프레임버퍼 메모리 읽기
3. 2×2 블록 평균 다운스케일 (`FB_SCALE=2`) → 전송 데이터 1/4 감소
4. RGB16/RGB32 → BGR24 변환 후 BMP 헤더 조립 (`BmpFileHeader` + `BmpDibHeader`, `#pragma pack(push,1)`)
5. HTTP 응답 헤더 + BMP 바이너리 전송

#### OOM 방지
- `pthread_mutex_t g_fb_mutex`: 동시 변환 1개로 제한 → 보드 메모리 부족 방지

---

### 멀티스레딩
| 스레드 | 생성 위치 | 역할 | 종료 방법 |
|--------|----------|------|----------|
| `gpioWatchThread` × 2 | 라운드 진입 시 | GPIO 버튼 감시 | `running.store(false)` + `join()` |
| `BgmPlayer::threadFunc` | `play()` 호출 시 | BGM 반복 재생 | `running = false` + `SIGTERM` + `join()` |
| `fb_server_run` | 게임 시작 시 | HTTP 모니터 서버 | 프로세스 종료까지 상시 실행 |
| HTTP 요청 핸들러 | 연결마다 | BMP 캡처/전송 | 요청 처리 후 자동 종료 (`detach`) |

---

### 시간 처리
| 항목 | 내용 |
|------|------|
| 타이머 | `std::chrono::steady_clock::now()` + `duration_cast<seconds/milliseconds>` |
| 논블로킹 대기 | `select()` 타임아웃 (5~200ms) |
| 차단 대기 | `usleep()` — 전환 화면(`showCorrectScreen` 3초 등) 중 사용, 이 시간 동안 UDP 수신 불가 |
| 캐릭터 표정 복귀 | `moodCryUntil = now() + 3s` → 매 루프에서 `now() >= moodCryUntil` 확인 |
