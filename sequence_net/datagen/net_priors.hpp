#pragma once
// =====================================================================
// SequenceNetV2 come sorgente dei prior per l'MCTS.
//
// La rete ordina le mosse, non valuta le foglie. Quella e' una scelta
// costretta, non di gusto: una valutazione costa 1,74 ms in WebAssembly
// contro 0,025 ms di una simulazione completa. Metterla su ogni foglia
// lascerebbe 115 foglie invece di 8.000. Sui prior invece ne bastano
// poche decine per ricerca, e il budget le regge.
//
// Le feature devono essere identiche a quelle con cui la rete e' stata
// addestrata (vedi NeuralAI::evaluate in ai.hpp): tre piani 10x10, le mie
// pedine, le sue, e le caselle che la mano corrente puo' giocare. Qui il
// formato e' NHWC perche' e' quello che vuole net.hpp.
//
// Nota: il terzo piano dipende dalla mano, quindi nemmeno il tronco della
// rete e' funzione della sola scacchiera. E' il motivo per cui una cache
// sulle posizioni non funziona: misurata, riusa lo 0,1%.
// =====================================================================

#include "moves.hpp"
#include "net.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

// Costruisce gli ingressi della rete. board: 300 float in NHWC (100 celle
// per 3 piani). handv: 52 float, quante copie di ogni carta ho in mano.
inline void netFeatures(Fast128 my, Fast128 opp, const std::vector<int>& hand,
                        float* board, float* handv) {
    Fast128 occupied = my | opp | MASK_CORNERS;
    Fast128 opp_locked = SequenceLogic::getLockedMask(opp);
    Fast128 playable = {0, 0};
    for (int c : hand) {
        if (c < 0) continue;
        if (CardTranslator::isTwoEyedJack(c))      playable |= (~occupied & MASK_BOARD);
        else if (CardTranslator::isOneEyedJack(c)) playable |= (opp & ~opp_locked);
        else                                       playable |= (CARD_MAP[c] & ~occupied);
    }
    std::fill(board, board + 300, 0.0f);
    std::fill(handv, handv + 52, 0.0f);
    for (int i = 0; i < 100; i++) {
        const uint64_t bit = (i < 64) ? (1ULL << i) : (1ULL << (i - 64));
        if (((i < 64) ? my.lo       : my.hi)       & bit) board[i * 3 + 0] = 1.0f;
        if (((i < 64) ? opp.lo      : opp.hi)      & bit) board[i * 3 + 1] = 1.0f;
        if (((i < 64) ? playable.lo : playable.hi) & bit) board[i * 3 + 2] = 1.0f;
    }
    for (int c : hand) if (c >= 0 && c < 52) handv[c] += 1.0f;
}

// I 200 logit della policy: i primi 100 sono le posate, i secondi 100 le
// rimozioni. Al chiamante serve solo un ordine, ci pensa l'MCTS a
// normalizzarli.
inline bool netPriors(const snet::Net& net, Fast128 my, Fast128 opp,
                      const std::vector<int>& hand,
                      const std::vector<Move>& moves, std::vector<float>& out) {
    float board[300], handv[52], policy[200], value;
    netFeatures(my, opp, hand, board, handv);
    net.forward(board, handv, policy, &value);
    out.resize(moves.size());
    for (size_t i = 0; i < moves.size(); i++)
        out[i] = policy[moves[i].pos + (moves[i].is_removal ? 100 : 0)];
    return true;
}
