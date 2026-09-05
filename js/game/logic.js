import { CONFIG, ASSETS } from './constants.js';

export class GameState {
    constructor() {
        this.grid = Array(CONFIG.BOARD_SIZE).fill(CONFIG.PLAYERS.NONE);
        this.locked = Array(CONFIG.BOARD_SIZE).fill(false);
        this.hands = { [CONFIG.PLAYERS.HUMAN]: [], [CONFIG.PLAYERS.AI]: [] };
        this.scores = { [CONFIG.PLAYERS.HUMAN]: 0, [CONFIG.PLAYERS.AI]: 0 };
        this.deck = [];
        this.currentPlayer = CONFIG.PLAYERS.HUMAN;
    }
}

export const Rules = {
    getValidMoves(state, card) {
        const moves = [];
        const is2Eyed = CONFIG.JACKS.TWO_EYED.includes(card);
        const is1Eyed = CONFIG.JACKS.ONE_EYED.includes(card);
        let isRemoval = is1Eyed;

        for (let i = 0; i < CONFIG.BOARD_SIZE; i++) {
            if (CONFIG.CORNERS.includes(i)) continue;
            const owner = state.grid[i];
            if (is2Eyed) {
                if (owner === 0) moves.push(i);
            } else if (is1Eyed) {
                if (owner !== 0 && owner !== state.currentPlayer && !state.locked[i]) moves.push(i);
            } else {
                if (ASSETS.BOARD_LAYOUT[i] === card && owner === 0) moves.push(i);
            }
        }
        return { validMoves: moves, isRemoval };
    },


    updateScore(state, lastPos) {
        const player = state.grid[lastPos];
        if (player === 0 || lastPos === -1) return;

        const W = CONFIG.GRID_COLS;
        const DIRS = [1, W, W + 1, W - 1]; // Orizzontale, Verticale, Diagonali

        DIRS.forEach(delta => {
            // 1. Estrai la linea contigua completa in questa direzione
            const line = this._getConnectedLine(state, lastPos, delta, player);
            
            // 2. Cerca sequenze valide all'interno della linea
            if (line.length >= 5) {
                this._processLineSequences(state, line, lastPos, player);
            }
        });
    },

    /**
     * Espande la ricerca a sinistra/destra (o su/giù) partendo da lastPos
     */
    _getConnectedLine(state, startPos, delta, player) {
        const W = CONFIG.GRID_COLS;
        const line = [startPos];

        // Helper per l'espansione in una direzione
        const expand = (dir) => {
            let curr = startPos;
            while (true) {
                const next = curr + (delta * dir);
                
                // Check confini array
                if (next < 0 || next >= CONFIG.BOARD_SIZE) break;
                
                // Check wrap-around (evita salti di riga errati sulle orizzontali/diagonali)
                if (Math.abs((curr % W) - (next % W)) > 1) break;

                // Check proprietario (Player o Angolo Jolly)
                const isOwned = state.grid[next] === player;
                const isCorner = CONFIG.CORNERS.includes(next);

                if (isOwned || isCorner) {
                    if (dir === -1) line.unshift(next); // Aggiungi in testa
                    else line.push(next);               // Aggiungi in coda
                    curr = next;
                } else {
                    break;
                }
            }
        };

        expand(-1); // Cerca "indietro"
        expand(1);  // Cerca "avanti"

        return line;
    },

    /**
     * Analizza la linea estratta usando una "sliding window" di 5.
     * Applica le regole di validazione (inclusione mossa corrente, max 1 locked).
     */
    _processLineSequences(state, line, lastPos, player) {
        // Scorre tutte le possibili sottosequenze di 5
        for (let i = 0; i <= line.length - 5; i++) {
            const subSeq = line.slice(i, i + 5);

            // la sequenza deve includere la pedina appena posata.
            if (!subSeq.includes(lastPos)) continue;

            // REGOLA 2: Controlliamo le pedine già bloccate (Overlap).
            // È permesso sovrascrivere al massimo 1 pedina bloccata (intersezione o sequenza da 9).
            let lockedCount = 0;
            subSeq.forEach(idx => {
                // Gli angoli non contano mai come "locked" ai fini dell'overlap
                if (state.locked[idx] && !CONFIG.CORNERS.includes(idx)) {
                    lockedCount++;
                }
            });

            if (lockedCount > 1) continue;

            // SEQUENZA VALIDA TROVATA!
            state.scores[player]++;
            
            // Blocca le pedine (tranne gli angoli)
            subSeq.forEach(idx => {
                if (!CONFIG.CORNERS.includes(idx)) state.locked[idx] = true;
            });
        }
    }
};