import { CONFIG, ASSETS } from './constants.js';
import { GameState, Rules } from './logic.js';
import { ApiClient } from '../core/api.js';

export class Game {
    // Tempo di ricerca del motore, in millisecondi.
    static THINK_MS = 200;     // per scegliere la mossa
    static WINRATE_MS = 70;    // per aggiornare la sola barra

    constructor(ui) {
        this.ui = ui;
        this.selIdx = -1;
        this.engine = null;   // motore MCTS in WebAssembly
    }

    start(restart = false) {
        const saved = localStorage.getItem('sequence_game_state');
        if (saved && !restart) {
            const parsed = JSON.parse(saved);
            this.state = new GameState();
            Object.assign(this.state, parsed);
            this.selIdx = -1;
            this.render();
            if (this.engine) {
                this.updatePlayerPerspectiveWinRate();
            }
            return;
        }

        this.state = new GameState();
        this.selIdx = -1;
        const suits = Object.keys(ASSETS.SUITS);
        const ranks = ASSETS.DECK_RANKS;
        const cards = suits.flatMap(s => ranks.map(r => r + s));
        this.state.deck = [...cards, ...cards];
        for (let i = 0; i < 3; i++) this._shuffle(this.state.deck);
        
        for (let i = 0; i < CONFIG.HAND_SIZE; i++) {
            this.state.hands[1].push(this.state.deck.pop());
            this.state.hands[2].push(this.state.deck.pop());
        }

        this.render();
        if (this.engine) {
            this.updatePlayerPerspectiveWinRate();
        }
    }

    // Carica il motore compilato in WebAssembly, poi i pesi di
    // SequenceNetV2 che gli servono per ordinare le mosse. Tutto servito dal
    // sito: nessuna CDN, nessun runtime ONNX.
    //
    // I pesi sono facoltativi. Se il file non arriva il motore resta quello
    // con i prior euristici, che gioca praticamente alla stessa forza: la
    // partita comincia lo stesso invece di non cominciare affatto.
    async loadModel() {
        const Module = await createSequenceModule();
        this.wasm = Module;
        this.engine = new Module.SequenceEngine();

        try {
            const res = await fetch("js/game/weights.bin?v=1");
            if (!res.ok) throw new Error("HTTP " + res.status);
            const buf = await res.arrayBuffer();
            if (buf.byteLength % 4 !== 0) throw new Error("file troncato");
            if (!this.engine.loadWeights(new Float32Array(buf))) {
                throw new Error("pesi rifiutati dal motore");
            }
            console.log("Motore MCTS (WebAssembly) con prior da SequenceNetV2.");
        } catch (err) {
            console.warn("Pesi della rete non caricati, si usano i prior euristici:",
                         err.message);
        }

        if (this.state) {
            await this.updatePlayerPerspectiveWinRate();
        }
    }

    // Costruisce gli argomenti per il motore dal punto di vista di `player`.
    // Il motore vuole 2 = chi sta chiedendo, 1 = l'avversario.
    _engineArgs(player) {
        const other = player === 1 ? 2 : 1;
        const grid = new this.wasm.VectorInt();
        for (let i = 0; i < 100; i++) {
            const owner = this.state.grid[i];
            grid.push_back(owner === player ? 2 : (owner === other ? 1 : 0));
        }
        const hand = new this.wasm.VectorString();
        for (const c of this.state.hands[player]) if (c && c !== '') hand.push_back(c);
        const oppHand = this.state.hands[other].filter(c => c && c !== '').length;
        return { grid, hand, oppHand, deck: this.state.deck.length };
    }

    render() {
        let moves = [];
        if (this.selIdx !== -1) {
            moves = Rules.getValidMoves(this.state, this.state.hands[1][this.selIdx]).validMoves;
        }
        this.ui.render(this.state, this.selIdx, moves);
    }

    clickHand(i) {
        if (this.state.currentPlayer !== 1) return;
        this.selIdx = (this.selIdx === i) ? -1 : i;
        this.render();
    }

    clickBoard(pos) {
        if (this.state.currentPlayer !== 1 || this.selIdx === -1) return;
        const card = this.state.hands[1][this.selIdx];
        const { validMoves, isRemoval } = Rules.getValidMoves(this.state, card);
        if (validMoves.includes(pos)) {
            this.executeMove(1, pos, this.selIdx, isRemoval);
        }
    }

    async executeMove(player, pos, cardIdx, isRemove) {
        if (isRemove) {
            this.state.grid[pos] = 0;
            this.state.locked[pos] = false;
        } else {
            this.state.grid[pos] = player;
        }
        this.state.hands[player][cardIdx] = this.state.deck.pop() || '';
        if (!isRemove) Rules.updateScore(this.state, pos);
        this.selIdx = -1;
        this.render();
        
        if (this.state.scores[player] >= CONFIG.SEQUENCES_TO_WIN) {
            this.state.currentPlayer = 0;
            const api = new ApiClient();
            const res = await api.sendGameResult(player === 1 ? "vittoria" : "sconfitta");
            if (!res.success) {
                alert("Errore invio risultato gioco: ");
            }
            this._saveGame();
            return this.ui.showEnd(player === 1 ? "HAI VINTO!" : "HAI PERSO.");
        }

        this.state.currentPlayer = player === 1 ? 2 : 1;
        this._replaceOneDeadCard(this.state.currentPlayer);
        this._saveGame();

        await this.updatePlayerPerspectiveWinRate(this._pendingAiWinRate ?? null);
        this._pendingAiWinRate = null;

        if (this.state.currentPlayer === 2) setTimeout(() => { this.aiMove(); }, 0);
    }

    // La barra non e' piu' la previsione di una rete: e' la frazione di
    // partite simulate che il giocatore vince. Una misura, non una stima.
    // Dopo la mossa dell'AI il numero c'e' gia' (basta rovesciarlo); negli
    // altri casi si fa una ricerca breve, che per la sola barra basta.
    async updatePlayerPerspectiveWinRate(aiWinRate = null) {
        if (!this.engine || !this.state) return;

        let p1;
        if (aiWinRate !== null) {
            p1 = Math.round((1 - aiWinRate) * 100);
        } else {
            const a = this._engineArgs(1);
            this.engine.computeBestMove(a.grid, a.hand, Game.WINRATE_MS, a.oppHand, a.deck);
            p1 = Math.round(this.engine.winRate() * 100);
            a.grid.delete(); a.hand.delete();
        }
        p1 = Math.min(100, Math.max(0, p1));

        this.state.winRate = { p1: p1, ai: 100 - p1 };
        this.render();
    }

    async aiMove() {
        if (!this.engine) {
            console.error("Motore non ancora caricato.");
            return;
        }

        const a = this._engineArgs(2);
        const mv = this.engine.computeBestMove(a.grid, a.hand, Game.THINK_MS, a.oppHand, a.deck);
        const aiWin = this.engine.winRate();
        a.grid.delete(); a.hand.delete();

        if (!mv || mv.pos < 0) {           // nessuna mossa: pesca e passa
            this.state.hands[2][0] = this.state.deck.pop() || '';
            this.state.currentPlayer = 1;
            this.render();
            return;
        }

        // Il motore riceve solo le carte giocabili, quindi il suo indice puo'
        // non coincidere con quello nella mano vera: si riallinea qui.
        const playable = [];
        this.state.hands[2].forEach((c, i) => { if (c && c !== '') playable.push(i); });
        const cardIdx = playable[mv.card_idx];
        if (cardIdx === undefined) {
            this.state.currentPlayer = 1;
            this.render();
            return;
        }

        this._pendingAiWinRate = aiWin;
        this.executeMove(2, mv.pos, cardIdx, mv.is_removal);
    }

    _shuffle(array) {
        for (let i = array.length - 1; i > 0; i--) {
            const j = Math.floor(Math.random() * (i + 1));
            [array[i], array[j]] = [array[j], array[i]];
        }
        return array;
    }

    _saveGame() {
        localStorage.setItem('sequence_game_state', JSON.stringify(this.state));
    }

    _replaceOneDeadCard(player) {
        for (let i = 0; i < this.state.hands[player].length; i++) {
            const card = this.state.hands[player][i];
            if (!card || card === '') continue;
            const { validMoves } = Rules.getValidMoves(this.state, card);
            const isJack = card.startsWith('J');
            
            if (validMoves.length === 0 && !isJack) {
                console.log(`[Giocatore ${player}] Carta morta sostituita in automatico: ${card}`);
                this.state.hands[player][i] = this.state.deck.pop() || '';
                break;
            }
        }
    }
}
