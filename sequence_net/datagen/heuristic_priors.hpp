#pragma once
// Prior euristici senza dipendenze: usati sia dal motore in WebAssembly
// sia dai confronti nativi, cosi' e' esattamente lo stesso codice.
#include "moves.hpp"
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------
// Prior euristici: servono solo a far guardare per prime le mosse sensate.
// Non c'e' bisogno che siano intelligenti quanto il vecchio motore: le
// minacce le trova la ricerca da sola, esplorando. Quindi niente scansione
// delle caselle libere, che qui verrebbe ripetuta in ogni nodo dell'albero.
// ---------------------------------------------------------------------
inline bool heuristicPriors(Fast128 my, Fast128 opp, const std::vector<int>& hand,
                            const std::vector<Move>& moves, std::vector<float>& out)
{
    (void)hand;
    out.resize(moves.size());
    const int myScore = SequenceLogic::calculateScore(my);

    for (size_t i = 0; i < moves.size(); i++) {
        const Move& m = moves[i];
        float s = 0.0f;

        if (m.is_removal) {
            // Togliere una pedina non costruisce niente: si parte sotto.
            Fast128 no = opp; clearBit(no, m.pos);
            s = -2.0f;
            // Salvo quando smonta una minaccia gia' formata
            if (SequenceLogic::calculateScore(no) < SequenceLogic::calculateScore(opp)) s += 8.0f;
        } else {
            Fast128 nm = my; setBit(nm, m.pos);
            const int after = SequenceLogic::calculateScore(nm);
            if (after >= 2)          s += 40.0f;   // chiude la partita
            else if (after > myScore) s += 12.0f;  // completa una sequenza
            // Le caselle che partecipano a piu' linee valgono di piu'.
            // Il peso e' gia' elevato al quadrato in board.hpp: si riscala.
            s += sqrtf((float)POSITION_WEIGHTS[m.pos]) * 0.35f;
        }
        out[i] = s;
    }
    return true;
}

