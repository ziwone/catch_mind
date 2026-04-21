# Catch Mind

## Overview
3-board real-time drawing & guessing game running on Linux framebuffer (aarch64).
- 1 drawer + 2 challengers, connected over a shared hub via UDP broadcast
- All interaction via touchscreen — no keyboard required during gameplay

Spec: `GAME_SPEC.md`

## How It Works
1. All 3 boards boot into the role selection screen simultaneously
2. First board to touch **LEFT** becomes the drawer; remaining boards auto-assign as challengers
3. Drawer picks one of 3 random categories, then one of 4 random words
4. Round starts — drawer's strokes are broadcast in real time to challenger screens
5. Challengers type answers on the touch keypad and submit
6. Drawer judges each answer (OK / NG)
   - Correct: round ends, scores updated, back to role selection
   - Wrong: answer panel clears, challenger retries
7. At 30 seconds remaining, a **HINT** (category name) appears in the info panel
8. Time up: answer is revealed, no score awarded
9. Game ends after all rounds; final scoreboard shown

## Build & Deploy

### On VirtualBox (cross-compile for aarch64)
```bash
bash /media/sf_share/catch_mind/deploy.sh
```
Runs `make`, copies the binary to `/nfsroot`, and syncs assets.

### On the board
```bash
cd /mnt/nfs
./catch_mind
```

## Word Bank
Categories and words are defined in `include/wordbank.h` (~40 words per category).
Current categories: `ANIMAL`, `FRUIT`, `FOOD`, `OBJECT`, `NATURE`, `SPORT`, `PLACE`

## Network
- Protocol: UDP broadcast, port 37031
- Board IPs: `192.168.10.3` (P1), `192.168.10.4` (P2), `192.168.10.5` (P3)
- Message format: `CM|<nodeId>|<kind>|<value>`

## Path Layout
```
/media/sf_share/catch_mind   Windows <-> VirtualBox shared folder (source)
/home/user/work/proj         VirtualBox working copy
/nfsroot                     VirtualBox NFS export root
/mnt/nfs                     Board NFS mount path (run from here)
```

