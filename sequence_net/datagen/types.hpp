#pragma once
#include <cstdint>

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

struct Move {
    int card_idx_in_hand;
    int pos;
    bool is_removal;
};

// Struttura binaria compatta per il datagen
#pragma pack(push, 1)
struct GameRecord {
    uint64_t my_board_lo;
    uint64_t my_board_hi;
    uint64_t opp_board_lo;
    uint64_t opp_board_hi;
    
    uint64_t playable_lo;
    uint64_t playable_hi;
    
    int8_t hand[7];
    int8_t hand_size;
    int8_t move_pos;
    int8_t is_removal;
    int8_t result;
};
#pragma pack(pop)