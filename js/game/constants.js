/**
 * ==========================================
 * 1. COSTANTI E CONFIGURAZIONE
 * ==========================================
 * Questo file contiene solo dati statici.
 * Se vuoi cambiare le regole base (es. grandezza mano), modificale qui.
 */
export const CONFIG = {
    AI_SIMULATIONS: 1000000,
    PLAYERS: { 
        NONE: 0,
        HUMAN: 1,
        AI: 2
    },
    HAND_SIZE: 7,
    BOARD_SIZE: 100,
    GRID_COLS: 10,
    CORNERS: [0, 9, 90, 99],
    SEQUENCES_TO_WIN: 2,
    JACKS: {
        TWO_EYED: ['JD', 'JC'],
        ONE_EYED: ['JH', 'JS']
    }
};

export const ASSETS = {
    SUITS: { 'D': '♦', 'C': '♣', 'H': '♥', 'S': '♠' },
    RANKS: ['2', '3', '4', '5', '6', '7', '8', '9', '10', 'Q', 'K', 'A'],
    DECK_RANKS: ['2', '3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K', 'A'],
    BOARD_LAYOUT: [
        "XX", "AC", "KC", "QC", "10C", "9C", "8C", "7C", "6C", "XX",
        "AD", "7S", "8S", "9S", "10S", "QS", "KS", "AS", "5C", "2S",
        "KD", "6S", "10C", "9C", "8C", "7C", "6C", "2D", "4C", "3S",
        "QD", "5S", "QC", "8H", "7H", "6H", "5C", "3D", "3C", "4S",
        "10D", "4S", "KC", "9H", "2H", "5H", "4C", "4D", "2C", "5S",
        "9D", "3S", "AC", "10H", "3H", "4H", "3C", "5D", "AH", "6S",
        "8D", "2S", "AD", "QH", "KH", "AH", "2C", "6D", "KH", "7S",
        "7D", "2H", "KD", "QD", "10D", "9D", "8D", "7D", "QH", "8S",
        "6D", "3H", "4H", "5H", "6H", "7H", "8H", "9H", "10H", "9S",
        "XX", "5D", "4D", "3D", "2D", "AS", "KS", "QS", "10S", "XX"
    ]
};