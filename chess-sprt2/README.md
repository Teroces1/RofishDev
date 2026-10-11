# SPRT tester for UCI chess engines

Runs candidate-vs-base matches between your engine executables, decides with an SPRT
whether a change is **better, worse, equal or inconclusive**, and shows everything in a
local web page.

## Run it

```
pip install -r requirements.txt
python app.py            # then open http://127.0.0.1:5000
```
On Windows you can double-click `start.bat` (it creates a virtual environment on first run).

1. Put engines in `engines/` (see `engines/README.txt`) or upload them on the **Engines** page.
2. Put your `openings.json` in `openings/`.
3. **Settings**: set parallel games, default move time and (optional) your Discord webhook.
4. **Tests -> New test**: pick base and candidate, press Create. Pause and resume any time.
   The **Self-test** checkbox plays an engine against itself to sanity-check the setup.

## What the engines must do

Only these commands are sent: `position fen <fen> [moves ...]`, `go movetime <ms>`, `quit`.
Every move sends the start FEN plus the full move list (opening included), so repetition
history is complete. Each game uses fresh engine processes, so no `reset` command is needed.
The tester reads `info ... score cp N` lines and the final `bestmove`. Lines containing
`[null]` are ignored, so the score comes from the last fully searched depth. Scores are read as
white's view by default (switchable to side-to-move in the settings).

## How games are refereed

python-chess is the referee. Neither engine is trusted:

* an illegal or unparseable move (including `a1a1`), a crash, or a hang past the hard timeout
  loses immediately and is logged with a full report (position, move list, raw output,
  exit code, legal moves) in the test's **Errors** tab and log;
* checkmate, stalemate, insufficient material, threefold repetition and the 50-move rule end
  the game; a move limit (default 600 plies) is a safety net;
* a test aborts after N engine errors (default 10), and Discord is notified;
* optional adjudication (off by default) ends lopsided or dead-drawn games early from engine
  scores. This one does trust the engines.

## Statistics

Each opening is played twice (candidate white, candidate black). The two games form a pair,
and results are analysed as pentanomial pair scores (win = 1 point, draw = 0.5, loss = 0).
Two SPRTs run on the same data:

* better: H0 gain = 0 vs H1 gain = +band
* worse: H0 gain = 0 vs H1 gain = -band
* equal: both accept H0
* inconclusive: nothing decided yet, or max games reached

`band` (default 5 Elo), alpha and beta (default 0.05) are per-test settings. The Elo estimate
and its 95% margin come from the same pair data. Changing the band when you resume re-evaluates
the stored games.

Rough sample sizes: about ±8-10 Elo margin after ~3,000 games, ±5 after 10,000+. Proving "equal
within ±5" therefore takes well over 10,000 games. With ~3,200 games of openings, use a wider
band (about 10) or add openings. Large gains and regressions resolve in a few hundred games.

## Pause, resume, add openings

All progress is stored in `data/sprt.db`. Resuming a test:

* never repeats an opening that was already played,
* appends openings that appeared in `openings/` since (shuffled),
* plays openings the max-games cap never reached before those.

A half-finished pair (one color played) is completed first. Pause waits for running games;
"Pause now" discards them (they are replayed on resume). If the server stops, tests come back paused.
Changing a test's engine executable after games exist is blocked unless you confirm, because new
games would not be comparable. Move time is locked once a test has games.

## Organising tests and engines

* Name engine folders `v012-feature-value`; group variants of one feature in a subfolder.
* **Experiments** group related tests (e.g. one per penalty value); the experiment page ranks them by Elo.
* The tests list can be filtered by base, candidate, experiment, status and verdict, and grouped by
  base or experiment. `#/tests?base=<engine id>` lists every test against one base.
* One test runs at a time; started tests wait in a queue.

## CPU use

Each worker plays one whole game, and both engines are pinned to the same logical CPU, so a game
is fair even on mixed fast and slow cores. Default CPU list: one logical processor per physical
core (for hybrid Intel CPUs: every second P-core thread, then the E-cores). Set "CPU list" in
Settings (e.g. `0,2,4,6,8,10,12,14` for P-cores only) if you want uniform speed. Keep workers below
your core count; movetime-based tests get noisy when the CPU is oversubscribed.

## Files

`app.py` Flask API · `sprt/game.py` referee · `sprt/stats.py` SPRT and Elo · `sprt/manager.py`
scheduler · `sprt/engines.py`, `sprt/openings.py` loaders · `static/` web UI.
