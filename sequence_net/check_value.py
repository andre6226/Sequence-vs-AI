"""
Controlla se la stima di vittoria di una rete e' calibrata.

Il test: su scacchiera vuota, con mani casuali, la rete dovrebbe dire circa
50% qualunque sia la mano. Se sputa valori sparsi fra 0 e 100 vuol dire che
la value e' satura e la percentuale mostrata sul sito oscillera' senza senso.

    python check_value.py rete.onnx [altra.onnx ...]

Riferimenti misurati: v2 (buona) dev.std 6.7 -- v3 (rotta) dev.std 26.3
"""
import sys
import random
import numpy as np
import onnxruntime as ort

LAYOUT = """XX AC KC QC 10C 9C 8C 7C 6C XX AD 7S 8S 9S 10S QS KS AS 5C 2S
KD 6S 10C 9C 8C 7C 6C 2D 4C 3S QD 5S QC 8H 7H 6H 5C 3D 3C 4S
10D 4S KC 9H 2H 5H 4C 4D 2C 5S 9D 3S AC 10H 3H 4H 3C 5D AH 6S
8D 2S AD QH KH AH 2C 6D KH 7S 7D 2H KD QD 10D 9D 8D 7D QH 8S
6D 3H 4H 5H 6H 7H 8H 9H 10H 9S XX 5D 4D 3D 2D AS KS QS 10S XX""".split()
CORNERS = {0, 9, 90, 99}
TWO_EYED = {"JD", "JC"}
RANKS = ["2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"]


def card_id(c):
    suit = {"D": 0, "C": 13, "H": 26, "S": 39}[c[-1]]
    r = c[:-1]
    rank = {"A": 12, "K": 11, "Q": 10, "J": 9}.get(r, None)
    return suit + (rank if rank is not None else int(r) - 2)


def empty_board_spread(path, n_hands=40, seed=7):
    sess = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
    deck = [r + s for s in "DCHS" for r in RANKS] * 2
    rng = random.Random(seed)
    vals = []
    for _ in range(n_hands):
        d = deck[:]
        rng.shuffle(d)
        hand = d[:7]
        board = np.zeros(300, dtype=np.float32)
        for c in hand:  # canale 2: caselle giocabili su scacchiera vuota
            for i in range(100):
                if i in CORNERS:
                    continue
                if c in TWO_EYED or LAYOUT[i] == c:
                    board[200 + i] = 1.0
        hv = np.zeros(52, dtype=np.float32)
        for c in hand:
            hv[card_id(c)] += 1.0
        v = sess.run(None, {"board_input": board.reshape(1, 3, 10, 10),
                            "hand_input": hv.reshape(1, 52)})[1][0][0]
        vals.append((v + 1) / 2 * 100)
    return np.array(vals)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    print(f"{'rete':<34} {'min':>5} {'max':>5} {'media':>6} {'dev.std':>8}  giudizio")
    for path in sys.argv[1:]:
        v = empty_board_spread(path)
        sd = v.std()
        verdetto = ("calibrata" if sd < 10 else
                    "accettabile" if sd < 15 else
                    "SATURA: la percentuale oscillera'")
        print(f"{path.split('/')[-1]:<34} {v.min():5.0f} {v.max():5.0f} "
              f"{v.mean():6.0f} {sd:8.1f}  {verdetto}")
