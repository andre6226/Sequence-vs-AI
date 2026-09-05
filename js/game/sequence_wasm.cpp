#include <emscripten/bind.h>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstring>

#define FORCE_INLINE inline __attribute__((always_inline))

struct alignas(16) Fast128 {
    uint64_t lo;
    uint64_t hi;

    Fast128() : lo(0), hi(0) {}
    Fast128(uint64_t l, uint64_t h) : lo(l), hi(h) {}

    FORCE_INLINE Fast128 operator&(Fast128 other) const { return { lo & other.lo, hi & other.hi }; }
    FORCE_INLINE Fast128 operator|(Fast128 other) const { return { lo | other.lo, hi | other.hi }; }
    FORCE_INLINE Fast128 operator^(Fast128 other) const { return { lo ^ other.lo, hi ^ other.hi }; }
    FORCE_INLINE Fast128 operator~() const { return { ~lo, ~hi }; }
    
    FORCE_INLINE void operator&=(Fast128 other) { lo &= other.lo; hi &= other.hi; }
    FORCE_INLINE void operator|=(Fast128 other) { lo |= other.lo; hi |= other.hi; }

    template<int N>
    FORCE_INLINE Fast128 shr() const {
        if (N == 0) return *this;
        if (N >= 64) return { hi >> (N - 64), 0 };
        return { (lo >> N) | (hi << (64 - N)), hi >> N };
    }

    template<int N>
    FORCE_INLINE Fast128 shl() const {
        if (N == 0) return *this;
        if (N >= 64) return { 0, lo << (N - 64) };
        return { lo << N, (hi << N) | (lo >> (64 - N)) };
    }
    
    FORCE_INLINE bool isZero() const { return (lo | hi) == 0; }
};

Fast128 SINGLE_BIT_MASKS[100]; 
Fast128 MASK_CORNERS;
Fast128 MASK_H_START, MASK_H9_START;
Fast128 MASK_D1_START, MASK_D2_START;
Fast128 MASK_BOARD;
Fast128 MASK_ALL_ONES = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFULL}; 

Fast128 CARD_MAP[52];
int POSITION_WEIGHTS[100];
bool CONSTANTS_INITIALIZED = false;

const char* OFFICIAL_BOARD_RAW[100] = {
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

FORCE_INLINE void setBit(Fast128& b, int p) {
    b |= SINGLE_BIT_MASKS[p];
}
FORCE_INLINE void clearBit(Fast128& b, int p) {
    Fast128 m = SINGLE_BIT_MASKS[p];
    b.lo &= ~m.lo;
    b.hi &= ~m.hi;
}

void initGameConstants() {
    if(CONSTANTS_INITIALIZED) return;
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

    static int calculateScore(Fast128 p_board) {
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

    static Fast128 getLockedMask(Fast128 p_board) {
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

struct Move {
    int card_idx_in_hand;
    int pos;
    bool is_removal;
};

struct FastRNG {
    uint64_t s;
    FastRNG(uint64_t seed) : s(seed + 1) {}
    FORCE_INLINE uint32_t next() {
        uint64_t x=s; x^=x<<13; x^=x>>7; x^=x<<17; s=x; return (uint32_t)x;
    }
    FORCE_INLINE int nextInt(int max) { return (int)(((uint64_t)next() * (uint64_t)max) >> 32); }
};

class MonteCarloAI {
    int8_t empty_slots[100]; 
    int8_t sim_slots[100]; 
    long long m_scores[256];
    bool m_isJack[12];
    std::vector<Move> m_moves_cache;

public:
    MonteCarloAI() { initGameConstants(); m_moves_cache.reserve(100); }

    Move runSimulation(const std::vector<Move>& moves, Fast128 my, Fast128 opp, const std::vector<std::string>& handStr, int simulations) {
        if(moves.empty()) return {-1, -1, false};

        size_t num_moves = moves.size();
        std::memset(m_scores, 0, num_moves * sizeof(long long));
        
        for(size_t i=0; i<handStr.size(); i++) {
             int cId = CardTranslator::toId(handStr[i]);
             m_isJack[i] = (cId != -1 && CardTranslator::isTwoEyedJack(cId));
        }

        int initial_opp_score = SequenceLogic::calculateScore(opp);
        int initial_my_score = SequenceLogic::calculateScore(my);
        FastRNG rng(12345);

        for(size_t m=0; m < num_moves; m++) {
            const Move& currentMove = moves[m];

            if (!currentMove.is_removal) {
                m_scores[m] += (long long)POSITION_WEIGHTS[currentMove.pos] * 800;
            }

            if (!handStr.empty() && currentMove.card_idx_in_hand < (int)handStr.size()) {
                if (m_isJack[currentMove.card_idx_in_hand]) m_scores[m] -= 50000;
            }

            Fast128 next_my = my;
            Fast128 next_opp = opp;
            
            if (currentMove.is_removal) clearBit(next_opp, currentMove.pos);
            else setBit(next_my, currentMove.pos);

            int my_score_after = SequenceLogic::calculateScore(next_my);

            if (my_score_after >= 2) { m_scores[m] += 100000000; continue; }
            if (my_score_after > initial_my_score) m_scores[m] += 30000000;

            Fast128 next_occupied = next_my | next_opp | MASK_CORNERS;
            bool danger = false;
            int current_opp_state_score = SequenceLogic::calculateScore(next_opp);

            Fast128 free_slots = ~next_occupied & MASK_BOARD;
            Fast128 temp_slots = free_slots;
            
            while(!temp_slots.isZero()) {
                int kill_p = BitScanner::next(temp_slots);
                Fast128 opp_after_kill = next_opp;
                setBit(opp_after_kill, kill_p);
                int opp_score_future = SequenceLogic::calculateScore(opp_after_kill);

                if (opp_score_future >= 2) { m_scores[m] -= 50000000; danger = true; break; }
                if (opp_score_future > current_opp_state_score) { m_scores[m] -= 20000000; danger = true; break; }
            }

            if(danger && my_score_after < 2) continue;

            int sims_per_move = simulations / num_moves;
            if (num_moves > 20) sims_per_move = std::max(100, sims_per_move);

            int base_empty_count = 0;
            temp_slots = free_slots;
            while(!temp_slots.isZero()) {
                empty_slots[base_empty_count++] = (int8_t)BitScanner::next(temp_slots);
            }

            for(int s=0; s < sims_per_move; s++) {
                Fast128 sm = next_my | MASK_CORNERS;
                Fast128 so = next_opp | MASK_CORNERS;
                if(my_score_after > initial_my_score) m_scores[m] += 2000000;

                std::memcpy(sim_slots, empty_slots, base_empty_count);
                int current_empty_count = base_empty_count;
                int depth = 0;

                while(depth < 24 && current_empty_count >= 2) {
                    int r1 = rng.nextInt(current_empty_count--);
                    int p1 = sim_slots[r1];
                    sim_slots[r1] = sim_slots[current_empty_count]; 
                    sm |= SINGLE_BIT_MASKS[p1];
                    if(SequenceLogic::checkWinFast(sm)) { m_scores[m] += 1000; break; }

                    int r2 = rng.nextInt(current_empty_count--);
                    int p2 = sim_slots[r2];
                    sim_slots[r2] = sim_slots[current_empty_count];
                    so |= SINGLE_BIT_MASKS[p2];
                    if(SequenceLogic::checkWinFast(so)) { m_scores[m] -= 5000; break; }
                    depth += 2;
                }
                m_scores[m] += (SequenceLogic::calculateScore(sm) - SequenceLogic::calculateScore(so)) * 200;
            }
        }

        int best = 0;
        long long maxS = -9e18;
        for(size_t i=0; i<num_moves; i++) { if(m_scores[i] > maxS) { maxS = m_scores[i]; best = i; } }
        return moves[best];
    }

    Move computeBestMove(const std::vector<int>& flatGrid, const std::vector<std::string>& handStr, int simulations) {
        if(flatGrid.size() != 100) return {-1, -1, false};

        Fast128 my = {0,0};
        Fast128 opp = {0,0};
        
        for(int i=0; i<100; i++) {
            if(flatGrid[i] == 2) setBit(my, i);
            else if(flatGrid[i] == 1) setBit(opp, i);
        }

        Fast128 occupied = my | opp | MASK_CORNERS;
        
        Fast128 opp_locked = SequenceLogic::getLockedMask(opp);

        m_moves_cache.clear();
        if (m_moves_cache.capacity() < 200) m_moves_cache.reserve(200);

        for(size_t i=0; i<handStr.size(); i++) {
            int cardID = CardTranslator::toId(handStr[i]);
            if (cardID == -1) continue;

            if (CardTranslator::isTwoEyedJack(cardID)) {
                Fast128 free_slots = ~occupied & MASK_BOARD;
                while(!free_slots.isZero()) {
                    m_moves_cache.push_back({(int)i, BitScanner::next(free_slots), false});
                }
            } else if (CardTranslator::isOneEyedJack(cardID)) {
                Fast128 targets = opp & ~opp_locked; 
                while(!targets.isZero()) {
                    m_moves_cache.push_back({(int)i, BitScanner::next(targets), true});
                }
            } else {
                Fast128 valid_pos = CARD_MAP[cardID] & ~occupied;
                while(!valid_pos.isZero()) {
                    m_moves_cache.push_back({(int)i, BitScanner::next(valid_pos), false});
                }
            }
        }
        return runSimulation(m_moves_cache, my, opp, handStr, simulations);
    }
};

EMSCRIPTEN_BINDINGS(sequence_module) {
    using namespace emscripten;
    value_object<Move>("Move").field("card_idx", &Move::card_idx_in_hand).field("pos", &Move::pos).field("is_removal", &Move::is_removal);
    class_<MonteCarloAI>("MonteCarloAI").constructor<>().function("computeBestMove", &MonteCarloAI::computeBestMove);
    register_vector<int>("VectorInt");
    register_vector<std::string>("VectorString");
}