# 캐치마인드 (Catch Mind)

## 개요
Linux 프레임버퍼(aarch64) 기반 3보드 실시간 그림 맞추기 게임.
- 출제자 1명 + 도전자 2명, 같은 허브의 보드들이 UDP 브로드캐스트로 연동
- 모든 조작은 터치스크린으로 처리 (게임 중 키보드 불필요)

상세 기획: `GAME_SPEC.md`

## 게임 흐름
1. 3개 보드가 동시에 역할 선택 화면으로 진입
2. 가장 먼저 **좌측**을 터치한 보드가 출제자, 나머지는 자동으로 도전자 배정
3. 출제자가 랜덤 3개 카테고리 중 하나를 고르고, 4개 단어 중 주제어를 선택
4. 라운드 시작 — 출제자의 드로잉이 도전자 화면에 실시간 반영
5. 도전자는 터치 키패드로 답을 입력하고 제출
6. 출제자가 각 답을 판정 (OK / NG)
   - 정답: 라운드 종료, 점수 반영, 역할 선택 화면으로 복귀
   - 오답: 해당 도전자 답변 패널 초기화, 재도전
7. 남은 시간 30초가 되면 왼쪽 정보 패널에 **힌트**(카테고리명) 표시
8. 시간 초과: 정답 공개, 점수 없음
9. 전체 라운드 종료 후 최종 점수판 표시

## 빌드 및 배포

### VirtualBox에서 (aarch64 크로스컴파일)
```bash
bash /media/sf_share/catch_mind/deploy.sh
```
`make` 실행 후 바이너리를 `/nfsroot`에 복사하고 에셋을 동기화합니다.

### 보드에서 실행
```bash
cd /mnt/nfs
./catch_mind
```

## 단어 은행
카테고리와 단어는 `include/wordbank.h`에서 관리 (카테고리당 약 40개).
현재 카테고리: `ANIMAL`, `FRUIT`, `FOOD`, `OBJECT`, `NATURE`, `SPORT`, `PLACE`

## 네트워크
- 프로토콜: UDP 브로드캐스트, 포트 37031
- 보드 IP: `192.168.10.3` (P1), `192.168.10.4` (P2), `192.168.10.5` (P3)
- 메시지 형식: `CM|<nodeId>|<kind>|<value>`

## 경로 구조
```
/media/sf_share/catch_mind   Windows ↔ VirtualBox 공유 폴더 (소스 편집)
/home/user/work/proj         VirtualBox 작업 복사본
/nfsroot                     VirtualBox NFS export 루트
/mnt/nfs                     보드 NFS 마운트 경로 (실행 위치)
```

