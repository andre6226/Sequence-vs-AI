import { CONFIG, ASSETS } from './constants.js';
import { GameState, Rules } from './logic.js';
import { ApiClient } from '../core/api.js';

export class Game {
    constructor(ui) {
        this.ui = ui;
        this.selIdx = -1;
        this.onnxSession = null;
    }

    start(restart = false) {
        const saved = localStorage.getItem('sequence_game_state');
        if (saved && !restart) {
            const parsed = JSON.parse(saved);
            this.state = new GameState();
            Object.assign(this.state, parsed);
            this.selIdx = -1;
            this.render();
            if (this.onnxSession) {
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
        if (this.onnxSession) {
            this.updatePlayerPerspectiveWinRate();
        }
    }

    async loadModel() {
        this.onnxSession = await ort.InferenceSession.create('./sequence_net.onnx', {
            executionProviders: ['wasm']
        });
        console.log("Rete Neurale caricata nel browser!");
        if (this.state) {
            await this.updatePlayerPerspectiveWinRate();
        }
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

        await this.updatePlayerPerspectiveWinRate();

        if (this.state.currentPlayer === 2) setTimeout(() => { this.aiMove(); }, 0);
    }

    async updatePlayerPerspectiveWinRate() {
        if (!this.onnxSession || !this.state) return;

        // 1. Canale 0 = Pedine Umano, Canale 1 = Pedine AI
        const boardArray = new Float32Array(300);
        for (let i = 0; i < 100; i++) {
            const owner = this.state.grid[i];
            if (owner === 1) boardArray[i] = 1.0;
            else if (owner === 2) boardArray[100 + i] = 1.0;
        }

        // Canale 2: Mosse lecite generate dalla mano del giocatore umano
        this.state.hands[1].forEach(card => {
            if (card && card !== '') {
                const { validMoves } = Rules.getValidMoves(this.state, card);
                validMoves.forEach(pos => {
                    boardArray[200 + pos] = 1.0;
                });
            }
        });

        // 2. Vettore one-hot a 52 dimensioni per la mano del giocatore
        const handArray = new Float32Array(52);
        for (const card of this.state.hands[1]) {
            const cardId = this._getCardId(card);
            if (cardId >= 0 && cardId <= 51) handArray[cardId] += 1.0;
        }

        // 3. Inferenza dal punto di vista del giocatore umano
        const feeds = {
            "board_input": new ort.Tensor('float32', boardArray, [1, 3, 10, 10]),
            "hand_input":  new ort.Tensor('float32', handArray, [1, 52])
        };

        const results = await this.onnxSession.run(feeds);
        const playerValue = results.value_output.data[0]; // Stima compresa in [-1.0, 1.0]

        const playerWinPct = Math.min(100, Math.max(0, Math.round(((playerValue + 1.0) / 2.0) * 100)));
        
        this.state.winRate = {
            p1: playerWinPct,
            ai: 100 - playerWinPct
        };

        this.render();
    }

    async aiMove() {
        if (!this.onnxSession) {
            console.error("Modello ONNX non ancora caricato!");
            return;
        }

        // 1. Inferenza iniziale della Policy per l'AI
        const { boardArray, handArray } = this._prepareTensorsForAI();
        const rootFeeds = {
            "board_input": new ort.Tensor('float32', boardArray, [1, 3, 10, 10]),
            "hand_input":  new ort.Tensor('float32', handArray, [1, 52])
        };
        const rootResults = await this.onnxSession.run(rootFeeds);
        const policy = rootResults.policy_output.data;

        // 2. Action Masking e raccolta mosse legali dell'AI
        const legalMoves = [];
        this.state.hands[2].forEach((card, cardIdx) => {
            const { validMoves, isRemoval } = Rules.getValidMoves(this.state, card);
            validMoves.forEach(pos => {
                const policyIndex = pos + (isRemoval ? 100 : 0);
                legalMoves.push({
                    pos,
                    cardIdx,
                    card,
                    isRemoval,
                    prior: policy[policyIndex]
                });
            });
        });

        if (legalMoves.length === 0) {
            this.state.hands[2][0] = this.state.deck.pop() || '';
            this.state.currentPlayer = 1;
            this.render();
            return;
        }

        legalMoves.sort((a, b) => b.prior - a.prior);
        const topCandidates = legalMoves.slice(0, 3);

        if (topCandidates.length === 1) {
            this.executeMove(2, topCandidates[0].pos, topCandidates[0].cardIdx, topCandidates[0].isRemoval);
            return;
        }

        // 3. Lookahead tattico a 1 semimossa
        let bestOverallScore = -Infinity;
        let chosenMove = topCandidates[0];

        for (const cand of topCandidates) {
            const simGrid = [...this.state.grid];
            if (cand.isRemoval) simGrid[cand.pos] = 0;
            else simGrid[cand.pos] = 2;

            const simHand = [...this.state.hands[2]];
            simHand.splice(cand.cardIdx, 1);

            const simBoardArray = new Float32Array(300);
            for (let i = 0; i < 100; i++) {
                if (simGrid[i] === 2) simBoardArray[i] = 1.0;
                else if (simGrid[i] === 1) simBoardArray[100 + i] = 1.0;
            }

            simHand.forEach(c => {
                if (c && c !== '') {
                    const tempState = { ...this.state, grid: simGrid };
                    const { validMoves } = Rules.getValidMoves(tempState, c);
                    validMoves.forEach(p => {
                        simBoardArray[200 + p] = 1.0;
                    });
                }
            });

            const simHandArray = new Float32Array(52);
            simHand.forEach(c => {
                const id = this._getCardId(c);
                if (id >= 0 && id <= 51) simHandArray[id] += 1.0;
            });

            const evalFeeds = {
                "board_input": new ort.Tensor('float32', simBoardArray, [1, 3, 10, 10]),
                "hand_input":  new ort.Tensor('float32', simHandArray, [1, 52])
            };

            const evalResults = await this.onnxSession.run(evalFeeds);
            const futureValue = evalResults.value_output.data[0];
            const combinedScore = futureValue + (cand.prior * 0.3);

            if (combinedScore > bestOverallScore) {
                bestOverallScore = combinedScore;
                chosenMove = cand;
            }
        }

        this.executeMove(2, chosenMove.pos, chosenMove.cardIdx, chosenMove.isRemoval);
    }

    _prepareTensorsForAI() {
        const boardArray = new Float32Array(300);
        for (let i = 0; i < 100; i++) {
            const owner = this.state.grid[i];
            if (owner === 2) boardArray[i] = 1.0;
            else if (owner === 1) boardArray[100 + i] = 1.0;
        }
        this.state.hands[2].forEach(card => {
            if (card && card !== '') {
                const { validMoves } = Rules.getValidMoves(this.state, card);
                validMoves.forEach(pos => {
                    boardArray[200 + pos] = 1.0;
                });
            }
        });
        const handArray = new Float32Array(52);
        for (const card of this.state.hands[2]) {
            const cardId = this._getCardId(card);
            if (cardId >= 0 && cardId <= 51) handArray[cardId] += 1.0;
        }
        return { boardArray, handArray };
    }

    _getCardId(cardStr) {
        if (!cardStr || cardStr === "XX") return -1;
        const suitChar = cardStr.slice(-1);
        const rankStr = cardStr.slice(0, -1);
        
        let suitOffset = 0;
        if (suitChar === 'D') suitOffset = 0;
        else if (suitChar === 'C') suitOffset = 13;
        else if (suitChar === 'H') suitOffset = 26;
        else if (suitChar === 'S') suitOffset = 39;

        let rankOffset = 0;
        if (rankStr === 'A') rankOffset = 12;
        else if (rankStr === 'K') rankOffset = 11;
        else if (rankStr === 'Q') rankOffset = 10;
        else if (rankStr === 'J') rankOffset = 9;
        else rankOffset = parseInt(rankStr, 10) - 2;

        return suitOffset + rankOffset;
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
