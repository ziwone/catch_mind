# CatchMind Game Specification

## Target Flow (Final)
1. Three players participate: 1 drawer and 2 guessers.
2. Role selection screen appears first.
3. After drawer is selected:
   - Choose category.
   - Show 4 random words from that category.
   - Drawer selects one target word.
4. Round starts:
   - Top area: drawing canvas.
   - Bottom area: two guess panels (player1/player2 answers).
5. Guessers can submit answers at any time.
6. Drawer judges each answer:
   - Wrong: round continues.
   - Correct: round ends immediately and goes back to role selection.

## Current Prototype (Single-board)
- Board `192.168.10.3` only.
- Drawer role only (role screen exists, drawer is selectable).
- Category + random 4 words + target selection implemented.
- 3-part UI layout implemented on framebuffer HDMI display:
  - Top: drawing canvas.
  - Bottom-left: player1 answer result panel.
  - Bottom-right: player2 answer result panel.
- Inputs are from Teraterm command line (not touch yet).
- Judging is automatic exact match for now.

## Prototype Commands
- Draw: `w`, `a`, `s`, `d`
- Pen toggle: `p`
- Clear canvas: `c`
- Color: `1` `2` `3` `4` `5`
- Player1 answer: `guess <word>`
- Player2 answer(simulated): `guess2 <word>`
- End round: `q`

## Next Steps
1. Replace terminal commands with touchscreen input event handling.
2. Add network sync between drawer board and two guesser boards.
3. Move judging authority to drawer side.
4. Add round timer and score.
