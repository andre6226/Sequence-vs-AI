#pragma once
#include "types.hpp"
#include <string>

// Variabili globali marcate come inline (C++17)
inline Fast128 SINGLE_BIT_MASKS[100]; 
inline Fast128 MASK_CORNERS;
inline Fast128 MASK_H_START, MASK_H9_START;
inline Fast128 MASK_D1_START, MASK_D2_START;
inline Fast128 MASK_BOARD;
inline Fast128 MASK_ALL_ONES = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFULL}; 

inline Fast128 CARD_MAP[52];
inline int POSITION_WEIGHTS[100];
inline bool CONSTANTS_INITIALIZED = false;

inline const char* OFFICIAL_BOARD_RAW[100] = {
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
};

struct BitScanner {
    static FORCE_INLINE int next(Fast128& b) {
        if (b.lo) {
            int bit = __builtin_ctzll(b.lo);
            b.lo &= ~(1ULL << bit);
            return bit;
        }
        if (b.hi) {
            int bit = __builtin_ctzll(b.hi);
            b.hi &= ~(1ULL << bit);
            return bit + 64;
        }
        return -1;
    }
};

FORCE_INLINE int countSetBits(Fast128 n) {
    return __builtin_popcountll(n.lo) + __builtin_popcountll(n.hi);
}

class CardTranslator {
public:
    static int toId(const std::string& card) {
        size_t len = card.length();
        if (len < 2) return -1;
        char suitChar = card[len - 1];
        char r0 = card[0];
        char r1 = (len > 2) ? card[1] : 0;
        if (suitChar >= 'a' && suitChar <= 'z') suitChar -= 32;
        int suitOffset = 0;
        switch(suitChar) {
            case 'D': suitOffset = 0; break;
            case 'C': suitOffset = 13; break;
            case 'H': suitOffset = 26; break;
            case 'S': suitOffset = 39; break;
            default: return -1;
        }
        int rank = 0;
        if (r1 == '0' || r1 == 'T') rank = 8;
        else {
            if (r0 >= '2' && r0 <= '9') rank = r0 - '2';
            else if (r0 == 'T') rank = 8;
            else if (r0 == 'J' || r0 == 'j') rank = 9;
            else if (r0 == 'Q' || r0 == 'q') rank = 10;
            else if (r0 == 'K' || r0 == 'k') rank = 11;
            else if (r0 == 'A' || r0 == 'a') rank = 12;
            else return -1;
        }
        return suitOffset + rank;
    }
    static FORCE_INLINE bool isTwoEyedJack(int id) { return (id == 9 || id == 22); }
    static FORCE_INLINE bool isOneEyedJack(int id) { return (id == 35 || id == 48); }
};

FORCE_INLINE void setBit(Fast128& b, int p) { b |= SINGLE_BIT_MASKS[p]; }
FORCE_INLINE void clearBit(Fast128& b, int p) {
    Fast128 m = SINGLE_BIT_MASKS[p];
    b.lo &= ~m.lo;
    b.hi &= ~m.hi;
}

inline void initGameConstants() {
    if(CONSTANTS_INITIALIZED) return;
    // ... [Incolla qui l'interno identico della tua funzione initGameConstants originale] ...
    for(int i=0; i<100; i++) {
        SINGLE_BIT_MASKS[i] = {0, 0};
        if(i < 64) SINGLE_BIT_MASKS[i].lo = (1ULL << i);
        else SINGLE_BIT_MASKS[i].hi = (1ULL << (i - 64));
    }
    MASK_CORNERS = {0, 0};
    setBit(MASK_CORNERS, 0); setBit(MASK_CORNERS, 9);
    setBit(MASK_CORNERS, 90); setBit(MASK_CORNERS, 99);

    for(int i=0; i<52; i++) CARD_MAP[i] = {0, 0};
    for(int i=0; i<100; i++) {
        std::string cardName = OFFICIAL_BOARD_RAW[i];
        if (cardName == "XX") continue;
        int cardID = CardTranslator::toId(cardName);
        if (cardID != -1) setBit(CARD_MAP[cardID], i);
    }
    MASK_BOARD = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFULL};
    MASK_H_START = {0,0}; MASK_H9_START = {0,0};
    MASK_D1_START = {0,0}; MASK_D2_START = {0,0};
    
    for (int r = 0; r < 10; r++) {
         for(int k=0; k<6; k++) setBit(MASK_H_START, r*10 + k);
         for(int k=0; k<2; k++) setBit(MASK_H9_START, r*10 + k);
         for(int k=0; k<6; k++) if(r<=5) setBit(MASK_D1_START, r*10 + k);
         for(int k=4; k<10; k++) if(r<=5) setBit(MASK_D2_START, r*10 + k);
    }
    for(int i=0; i<100; i++) POSITION_WEIGHTS[i] = 0;
    auto addLineWeight = [&](int r_start, int c_start, int dr, int dc) {
        int idxs[5];
        bool hasCorner = false;
        for(int k=0; k<5; k++) {
            idxs[k] = (r_start + k*dr) * 10 + (c_start + k*dc);
            if (idxs[k] == 0 || idxs[k] == 9 || idxs[k] == 90 || idxs[k] == 99) hasCorner = true;
        }
        bool isDiagonal = (dr != 0 && dc != 0);
        int weight = 3; 
        if (isDiagonal && hasCorner) weight = 12;
        else if (!isDiagonal && hasCorner) weight = 6; 
        else if (isDiagonal) weight = 4; 
        for(int k=0; k<5; k++) POSITION_WEIGHTS[idxs[k]] += weight;
    };
    for(int r=0; r<10; r++) for(int c=0; c<=5; c++) addLineWeight(r, c, 0, 1);
    for(int c=0; c<10; c++) for(int r=0; r<=5; r++) addLineWeight(r, c, 1, 0);
    for(int r=0; r<=5; r++) for(int c=0; c<=5; c++) addLineWeight(r, c, 1, 1);
    for(int r=0; r<=5; r++) for(int c=4; c<10; c++) addLineWeight(r, c, 1, -1);
    for(int i=0; i<100; i++) POSITION_WEIGHTS[i] *= POSITION_WEIGHTS[i];
    POSITION_WEIGHTS[0]=0; POSITION_WEIGHTS[9]=0; POSITION_WEIGHTS[90]=0; POSITION_WEIGHTS[99]=0;
    CONSTANTS_INITIALIZED = true;
}

class SequenceLogic {
public:
    template <int STEP>
    static FORCE_INLINE Fast128 removeOverlaps(Fast128 b) {
        return b & ~(b.shr<STEP>());
    }

    template <int STEP>
    static FORCE_INLINE int evalLine(Fast128 b, Fast128 mask5, Fast128 mask9) {
        Fast128 t2 = b & b.shr<STEP>();
        if (t2.isZero()) return 0;
        Fast128 t4 = t2 & t2.shr<2 * STEP>();
        Fast128 starts5 = t4 & b.shr<4 * STEP>();
        starts5 &= mask5;
        
        if (starts5.isZero()) return 0;
        Fast128 starts9 = starts5 & starts5.shr<4 * STEP>();
        starts9 &= mask9;

        int score9 = 0;
        int score5 = 0;
        if (starts9.isZero()) {
            score5 = countSetBits(removeOverlaps<STEP>(starts5));
        } else {
            score9 = countSetBits(removeOverlaps<STEP>(starts9));
            Fast128 overlap = starts9 | starts9.shl<4 * STEP>(); 
            Fast128 starts5_pure = starts5 & ~overlap;
            score5 = countSetBits(removeOverlaps<STEP>(starts5_pure));
        }
        return score5 + 2 * score9;
    }

    template <int STEP>
    static FORCE_INLINE void accumulateLocked(Fast128 b, Fast128 mask5, Fast128& lockedAcc) {
        Fast128 t2 = b & b.shr<STEP>();
        Fast128 t4 = t2 & t2.shr<2 * STEP>();
        Fast128 starts5 = t4 & b.shr<4 * STEP>();
        starts5 &= mask5;

        if (!starts5.isZero()) {
            lockedAcc |= starts5;
            lockedAcc |= starts5.shl<STEP>();
            lockedAcc |= starts5.shl<2*STEP>();
            lockedAcc |= starts5.shl<3*STEP>();
            lockedAcc |= starts5.shl<4*STEP>();
        }
    }

    static inline int calculateScore(Fast128 p_board) {
        Fast128 b = p_board | MASK_CORNERS;
        int score = 0;
        score += evalLine<1>(b, MASK_H_START, MASK_H9_START);
        if (score >= 2) return score;
        score += evalLine<10>(b, MASK_BOARD, MASK_BOARD);
        if (score >= 2) return score;
        score += evalLine<11>(b, MASK_D1_START, MASK_ALL_ONES);
        if (score >= 2) return score;
        score += evalLine<9>(b, MASK_D2_START, MASK_ALL_ONES);
        return score;
    }

    static inline Fast128 getLockedMask(Fast128 p_board) {
        Fast128 b = p_board | MASK_CORNERS;
        Fast128 locked = {0, 0};
        
        accumulateLocked<1>(b, MASK_H_START, locked);
        accumulateLocked<10>(b, MASK_BOARD, locked);
        accumulateLocked<11>(b, MASK_D1_START, locked);
        accumulateLocked<9>(b, MASK_D2_START, locked);
        
        locked.lo &= ~MASK_CORNERS.lo;
        locked.hi &= ~MASK_CORNERS.hi;
        return locked;
    }

    static FORCE_INLINE bool checkWinFast(Fast128 b_with_corners) {
        int score = 0;
        score += evalLineFast<1>(b_with_corners, MASK_H_START);
        if (score >= 2) return true;
        score += evalLineFast<10>(b_with_corners, MASK_BOARD);
        if (score >= 2) return true;
        score += evalLineFast<11>(b_with_corners, MASK_D1_START);
        if (score >= 2) return true;
        score += evalLineFast<9>(b_with_corners, MASK_D2_START);
        return score >= 2;
    }

    template <int STEP>
    static FORCE_INLINE int evalLineFast(Fast128 b, Fast128 mask5) {
        Fast128 t2 = b & b.shr<STEP>();
        Fast128 t4 = t2 & t2.shr<2 * STEP>();
        Fast128 starts5 = t4 & b.shr<4 * STEP>();
        starts5 &= mask5;
        Fast128 clean = starts5 & ~(starts5.shr<STEP>());
        return countSetBits(clean);
    }

    static FORCE_INLINE bool checkWin(Fast128 b) { return calculateScore(b) >= 2; }
};