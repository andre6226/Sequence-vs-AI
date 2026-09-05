import { CONFIG, ASSETS } from './constants.js';

const UITEMPLATES = {
    // seme
    cardContent: (rank, suitSym, colorClass, suitClass) => `
        <div class="rank ${colorClass}">${rank}</div>
        <div class="${suitClass} ${colorClass}">${suitSym}</div>
    `,

    // container carta
    card: (content, index, isSelected = false) => `
        <div class="hand-card ${isSelected ? 'selected' : ''}" data-index="${index}">
            ${content}
        </div>
    `,

    // pedine
    chip: (type) => type ? `<div class="chip ${type}"></div>` : '',

    // celle della board
    boardCell: (index, classes, content, chipHtml) => `
        <div class="cell ${classes}" data-index="${index}">
            ${content}
            ${chipHtml}
        </div>
    `,
    
    // angoli
    corner: () => '<div class="corner-text"></div>'
};


export class UI {
    constructor(callbacks) {
        this.cb = callbacks;
        this.els = {
            board: document.getElementById('board'),
            hand: document.getElementById('hand-container'),
            stats: { 
                p1: document.getElementById('win-p1'), 
                ai: document.getElementById('win-ai')
            },
            modal: document.getElementById('modal-overlay'),
            msg: document.getElementById('game-result-msg'),
            restartButtons: document.querySelectorAll('.restart-btn')                     
        };

        this._initEventListeners();
    }

    _initEventListeners() {
        this._bindClick(this.els.board, '.cell', this.cb.onBoardClick);
        this._bindClick(this.els.hand, '.hand-card', this.cb.onHandClick);
        this.els.restartButtons.forEach(btn => {
            btn.addEventListener('click', () => this._handleRestart());
        });
    }

    _handleRestart() {
        this.els.modal.classList.add('hidden');
        this.els.modal.classList.remove('active');
        if (this.cb.onRestart) {
            this.cb.onRestart(true);
        }
    }   


    _bindClick(container, selector, callback) {
        if (!container) return;
        container.addEventListener('click', (e) => {
            const target = e.target.closest(selector);
            if (target && callback) {
                const idx = parseInt(target.dataset.index, 10);
                if (!isNaN(idx)) callback(idx);
            }
        });
    }

    render(state, selectedIdx, validMoves = []) {
        this._renderBoard(state, selectedIdx, validMoves);
        this._renderHand(state.hands[1], selectedIdx);
        this._updateStats(state, selectedIdx, validMoves);
    }


    _renderBoard(state, selectedIdx, validMoves) {
        this.els.board.innerHTML = state.grid.map((owner, i) => {
            const cardCode = ASSETS.BOARD_LAYOUT[i];
            const isCorner = CONFIG.CORNERS.includes(i);
            
            const highlightClass = this._getBoardHighlight(state, i, validMoves, selectedIdx);
            const chipType = this._getChipType(owner, state.locked[i]);
            const baseClass = isCorner ? 'corner' : '';
            const fullClasses = [baseClass, highlightClass].filter(Boolean).join(' ');

            const content = isCorner ? UITEMPLATES.corner() : this._getCardHtml(cardCode, true);

            return UITEMPLATES.boardCell(i, fullClasses, content, UITEMPLATES.chip(chipType));
        }).join('');
    }

    _renderHand(hand, selectedIdx) {
        this.els.hand.innerHTML = hand.map((cardCode, i) => {
            const content = this._getCardHtml(cardCode, false);
            // Non passiamo più 'window.onHandClick'
            return UITEMPLATES.card(content, i, i === selectedIdx);
        }).join('');
    }

    _updateStats(state, selectedIdx, validMoves) {
        if (state.winRate) {
            this.els.stats.p1.innerText = `${state.winRate.p1}%`;
            this.els.stats.ai.innerText = `${state.winRate.ai}%`;
        }
    }

    _getCardHtml(cardCode, isCenter) {
        if (!cardCode || cardCode === "XX") return '';
        
        const rank = cardCode.slice(0, -1);
        const suitChar = cardCode.slice(-1);
        const suitSym = ASSETS.SUITS[suitChar] || suitChar;
        
        const isRed = ['D', 'H'].includes(suitChar);
        const colorClass = isRed ? 'suit-red' : 'suit-black';
        const suitClass = isCenter ? 'suit-center' : 'suit';

        return UITEMPLATES.cardContent(rank, suitSym, colorClass, suitClass);
    }

    _getBoardHighlight(state, index, validMoves, selectedIdx) {
        if (!validMoves.includes(index)) return '';

        const cardInHand = state.hands[1][selectedIdx];
        const isOneEyed = CONFIG.JACKS.ONE_EYED.includes(cardInHand);
        
        return (state.currentPlayer === 1 && isOneEyed) ? 'highlight-remove' : 'highlight-valid';
    }


    _getChipType(owner, isLocked) {
        if (!owner) return null;
        const type = (owner === 1) ? 'player' : 'ai';
        return isLocked ? `${type} locked` : type;
    }

    showEnd(msg) {
        this.els.modal.classList.remove('hidden');
        this.els.modal.classList.add('active');
        this.els.msg.innerText = msg;
    }
}
