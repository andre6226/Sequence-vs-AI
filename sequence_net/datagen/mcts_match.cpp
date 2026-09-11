// MCTS guidato dalla rete contro la rete da sola.
//   ./mcts_match <rete.onnx> <mazzi> [ms] [c_puct] [prior_visits]
#include "ai.hpp"
#include "mcts.hpp"
#include "heuristic_priors.hpp"
#include <string>
#include <cstdio>
#include <random>

int main(int argc, char** argv) {
    initGameConstants();
    int N = (argc >= 3) ? std::stoi(argv[2]) : 50;
    MctsConfig cfg;
    if (argc >= 4) cfg.budget_ms   = std::stod(argv[3]);
    if (argc >= 5) cfg.c_puct      = std::stod(argv[4]);
    if (argc >= 6) cfg.prior_visits= std::stoi(argv[5]);
    std::string mode = (argc >= 7) ? argv[6] : "net";   // net | heuristic | uniform

    NeuralAI netA(argv[1], true, 61), netB(argv[1], true, 62);

    // I prior dalla policy della rete: e' la sorgente sostituibile del motore.
    PriorFn netPriors = [&netA](Fast128 my, Fast128 opp, const std::vector<int>& hand,
                                const std::vector<Move>& moves, std::vector<float>& out) {
        float pol[200];
        netA.evaluate(my, opp, hand, pol);
        out.resize(moves.size());
        for (size_t i = 0; i < moves.size(); i++)
            out[i] = pol[moves[i].pos + (moves[i].is_removal ? 100 : 0)];
        return true;
    };

    PriorFn chosen;
    if (mode == "net") chosen = netPriors;
    else if (mode == "heuristic") chosen = heuristicPriors;
    Mcts mcts(cfg, chosen);

    long simSum = 0, evalSum = 0, decisions = 0; double msSum = 0;
    int mw = 0, nw = 0, dr = 0;

    for (int g = 0; g < N; g++) {
      for (int mf = 0; mf < 2; mf++) {
        std::mt19937 rng(40000 + g);
        mcts.seed(9000 + g * 17 + mf);
        std::vector<int> deck;
        for (int i = 0; i < 52; i++) { deck.push_back(i); deck.push_back(i); }
        std::shuffle(deck.begin(), deck.end(), rng);
        Fast128 bm = {0,0}, bn = {0,0};
        std::vector<int> hm, hn;
        for (int i = 0; i < 7; i++) {
            hm.push_back(deck.back()); deck.pop_back();
            hn.push_back(deck.back()); deck.pop_back();
        }
        bool mturn = (mf == 0);
        int ply = 0, winner = 0;
        while (ply < 200) {
            if (mturn) {
                MctsStats st;
                Move m = mcts.search(bm, bn, hm, hn.size(), deck.size(), &st);
                if (m.pos == -1) break;
                simSum += st.simulations; evalSum += st.prior_calls;
                msSum += st.elapsed_ms; decisions++;
                if (m.is_removal) clearBit(bn, m.pos); else setBit(bm, m.pos);
                hm.erase(hm.begin() + m.card_idx_in_hand);
                if (!deck.empty()) { hm.push_back(deck.back()); deck.pop_back(); }
                if (SequenceLogic::checkWin(bm)) { winner = 1; break; }
            } else {
                Move m = netB.search1Ply(bn, bm, hn, ply, true);
                if (m.pos == -1) break;
                if (m.is_removal) clearBit(bm, m.pos); else setBit(bn, m.pos);
                hn.erase(hn.begin() + m.card_idx_in_hand);
                if (!deck.empty()) { hn.push_back(deck.back()); deck.pop_back(); }
                if (SequenceLogic::checkWin(bn)) { winner = -1; break; }
            }
            mturn = !mturn; ply++;
        }
        if (winner == 1) mw++; else if (winner == -1) nw++; else dr++;
      }
    }
    int t = mw + nw + dr;
    printf("  MCTS (%.0f ms/mossa, prior %s) contro v2 normale\n",
           cfg.budget_ms, mode.c_str());
    printf("    MCTS : %4d  (%.1f%%)\n", mw, 100.0 * mw / t);
    printf("    v2   : %4d  (%.1f%%)\n", nw, 100.0 * nw / t);
    printf("    patte: %4d  (%.1f%%)\n", dr, 100.0 * dr / t);
    printf("    per mossa: %.0f simulazioni, %.1f chiamate ai prior, %.0f ms\n",
           (double)simSum / decisions, (double)evalSum / decisions, msSum / decisions);
    return 0;
}
