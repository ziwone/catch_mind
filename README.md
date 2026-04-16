# 캐치마인드 (Catch Mind)

## 프로젝트 목표
- 최종 목표: 3보드 기반(출제자 1, 참가자 2) 실시간 캐치마인드
- 현재 단계: 1보드 단독 프로토타입(출제자 보드 + Teraterm 입력)

상세 기획 문서: `GAME_SPEC.md`

## 현재 동작(Prototype)
1. 역할 선택 화면 진입 (ROLE SELECT / DRAWER / CHALLENGER 버튼 터치)
2. 카테고리 선택 (터치)
3. 카테고리 내 랜덤 제시어 4개 중 1개 선택 (터치)
4. 라운드 시작
    - HDMI 화면 상단: 그림 캔버스
    - HDMI 화면 하단: 참가자 2명 정답 패널
5. 주제어 선택 중 도전자 화면에는 `WORD SELECTING...` 메시지 표시
6. Teraterm 명령으로 그리기/정답 입력
7. 정답이면 즉시 라운드 종료 후 역할 선택 화면으로 복귀

## 실행 방법

### 1) VirtualBox에서 빌드 + 배포
```bash
bash /media/sf_share/catch_mind/deploy.sh
```

### 2) 보드에서 게임 실행
```bash
cd /mnt/nfs
./catch_mind
```

### 3) 게임 명령어 (Teraterm)
```text
역할 선택 화면: 좌측 박스 터치(출제자), 우측 박스 터치(도전자)
카테고리/주제어 화면: 목록 터치로 선택
touch drag      상단 캔버스에 그림 그리기
p               펜 on/off
c               캔버스 초기화
1 2 3 4 5       색상 선택
guess <word>    참가자1 정답 제출
guess2 <word>   참가자2 정답 제출(시뮬레이션)
q               라운드 종료
```

## 3보드 통신 송신 확인 도구(일방 전송)

VB에서:
```bash
cd /home/user/work/proj
./cm_net_ping_host send 25000 192.168.10.3 192.168.10.4 192.168.10.5
```

출력의 `OK`는 커널 기준 전송 호출 성공을 의미하며, 수신측 응답 확인은 하지 않음.

## 경로 구조
```text
/media/sf_share/catch_mind   Windows↔VB 공유폴더(소스 편집)
/home/user/work/proj         VB 작업폴더(deploy.sh 동기화)
/nfsroot                     VB NFS export 경로
/mnt/nfs                     보드 NFS mount 경로(실행 위치)
```

