import os
import subprocess

STOCKFISH_PATH = "/mnt/c/Users/teroc/Documents/stockfish/stockfish-windows-x86-64-avx2.exe"
MY_ENGINE_PATH = "./test.exe"

START_FEN = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10" #"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
MAX_DEPTH = 5


class UCIEngine:
    def __init__(self, path):
        self.path = path
        self.process = subprocess.Popen(
            [path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )
        self._sync_uci()

    def _sync_uci(self):
        """Sends uci and isready commands to ensure engine is responsive."""
        self.send("uci")
        while True:
            line = self.readline()
            if line.startswith("uciok"):
                break
        self.send("isready")
        while True:
            line = self.readline()
            if line.startswith("readyok"):
                break

    def send(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def readline(self):
        return self.process.stdout.readline().strip()

    def run_perft(self, fen, move_history, depth):
        if not move_history:
            cmd = f"position fen {fen}"
        else:
            moves_str = " ".join(move_history)
            cmd = f"position fen {fen} moves {moves_str}"

        self.send(cmd)
        self.send(f"go perft {depth}")

        move_counts = {}
        total_nodes = 0

        while True:
            line = self.readline()
            if not line:
                continue

            # Parse move line: e.g. "e2e4: 20" or "e2e4 20"
            if "Nodes searched:" in line or "Nodes:" in line:
                try:
                    total_nodes = int(line.split(":")[-1].strip())
                except ValueError:
                    pass
            elif ":" in line:
                parts = line.split(":")
                move = parts[0].strip()
                try:
                    count = int(parts[1].strip().split()[0])
                    move_counts[move] = count
                except (ValueError, IndexError):
                    pass
            elif line.startswith("Stockfish") or line.startswith("info"):
                continue

            # Completion marker
            if line.startswith("Nodes searched") or line.startswith("Total nodes") or "nodes" in line.lower():
                break

        return total_nodes, move_counts

    def close(self):
        try:
            self.send("quit")
        except Exception:
            pass
        self.process.terminate()


def isolate_perft_mismatch(sf, my_engine, fen, history, depth, indent_level=0):
    """Recursively traces down to depth 1 to pinpoint the first faulty move."""
    indent = "  " * indent_level
    prefix = f"{indent}[Depth {depth}]"

    sf_nodes, sf_moves = sf.run_perft(fen, history, depth)
    my_nodes, my_moves = my_engine.run_perft(fen, history, depth)

    print(f"{prefix} Total Nodes -> Stockfish: {sf_nodes} | Your Engine: {my_nodes}")

    if sf_nodes == my_nodes and sf_moves == my_moves:
        print(f"{prefix} MATCH! All node counts align on this sub-branch.")
        return

    all_moves = set(sf_moves.keys()).union(set(my_moves.keys()))

    mismatched_moves = []
    missing_moves = []
    extra_moves = []

    for move in sorted(all_moves):
        sf_cnt = sf_moves.get(move, None)
        my_cnt = my_moves.get(move, None)

        if sf_cnt is None:
            extra_moves.append(move)
        elif my_cnt is None:
            missing_moves.append(move)
        elif sf_cnt != my_cnt:
            mismatched_moves.append((move, sf_cnt, my_cnt))

    # Reached depth 1: output the specific root level fault details
    if depth == 1:
        print(f"\n{indent}================ ROOT FAULT FOUND ================")
        if missing_moves:
            print(f"{indent} Missing Move(s) in Your Engine: {', '.join(missing_moves)}")
        if extra_moves:
            print(f"{indent} Extra Move(s) in Your Engine: {', '.join(extra_moves)}")
        if mismatched_moves:
            for move, sf_c, my_c in mismatched_moves:
                print(f"{indent} Count Mismatch on '{move}': Stockfish={sf_c}, Yours={my_c}")
        print(f"{indent}==================================================\n")
        return

    # Pick the first mismatch branch to descend down
    target_move = None
    if missing_moves:
        target_move = missing_moves[0]
        print(f"{prefix} Discrepancy detected: Your engine is missing move '{target_move}'.")
    elif extra_moves:
        target_move = extra_moves[0]
        print(f"{prefix} Discrepancy detected: Your engine generated extra move '{target_move}'.")
    elif mismatched_moves:
        target_move = mismatched_moves[0][0]
        sf_cnt, my_cnt = mismatched_moves[0][1], mismatched_moves[0][2]
        print(f"{prefix} Discrepancy detected on '{target_move}' (SF: {sf_cnt}, Mine: {my_cnt}). Navigating deeper...")

    if target_move:
        new_history = history + [target_move]
        isolate_perft_mismatch(sf, my_engine, fen, new_history, depth - 1, indent_level + 1)


def run_perft_incremental(sf, my_engine, fen, max_depth):
    """Iterates from depth 1 up to max_depth until a mismatch is encountered."""
    print(f"Testing FEN: {fen}\n" + "-" * 60)

    for depth in range(1, max_depth + 1):
        print(f"\n---> Testing Perft at Depth {depth} <---")
        sf_nodes, sf_moves = sf.run_perft(fen, [], depth)
        my_nodes, my_moves = my_engine.run_perft(fen, [], depth)

        print(f"Stockfish Nodes: {sf_nodes} | Your Engine Nodes: {my_nodes}")

        if sf_nodes != my_nodes or sf_moves != my_moves:
            print(f"\nMISMATCH DETECTED AT DEPTH {depth}! Isolating faulty move sequence...\n")
            isolate_perft_mismatch(sf, my_engine, fen, [], depth)
            return

        print(f"Depth {depth} PASSED.")

    print(f"\nAll depth checks up to Depth {max_depth} passed successfully!")


def main():
    if not os.path.exists(STOCKFISH_PATH):
        print(f"Error: Could not find Stockfish at {STOCKFISH_PATH}")
        return
    if not os.path.exists(MY_ENGINE_PATH):
        print(f"Error: Could not find engine at {MY_ENGINE_PATH}")
        return

    print("Initializing UCI Engines under WSL...")
    sf = UCIEngine(STOCKFISH_PATH)
    my_engine = UCIEngine(MY_ENGINE_PATH)

    try:
        run_perft_incremental(sf, my_engine, START_FEN, MAX_DEPTH)
    finally:
        sf.close()
        my_engine.close()


if __name__ == "__main__":
    main()