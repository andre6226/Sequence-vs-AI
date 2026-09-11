#pragma once
// Generazione delle mosse legali: sola logica di gioco, nessuna dipendenza
// dalla rete neurale. Sta in un header a parte proprio per questo, cosi' il
// motore di ricerca puo' essere compilato anche senza ONNX Runtime (per
// esempio quando finisce in WebAssembly).
#include "board.hpp"
#include <vector>

inline std::vector<Move> legalMovesFor(Fast128 my, Fast128 opp, const std::vector<int>& hand) {
    Fast128 occupied = my | opp | MASK_CORNERS;
    Fast128 opp_locked = SequenceLogic::getLockedMask(opp);
    std::vector<Move> out;
    out.reserve(64);
    for (size_t i = 0; i < hand.size(); i++) {
        int cardID = hand[i];
        if (cardID < 0) continue;
        Fast128 mv = {0, 0};
        bool is_rem = false;
        if (CardTranslator::isTwoEyedJack(cardID)) {
            mv = (~occupied & MASK_BOARD);
        } else if (CardTranslator::isOneEyedJack(cardID)) {
            mv = (opp & ~opp_locked);
            is_rem = true;
        } else {
            mv = (CARD_MAP[cardID] & ~occupied);
        }
        Fast128 t = mv;
        while (!t.isZero()) out.push_back({ (int)i, BitScanner::next(t), is_rem });
    }
    return out;
}
