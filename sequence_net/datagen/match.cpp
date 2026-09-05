// Confronto diretto fra due reti: le fa giocare una contro l'altra in
// modalita' greedy, ogni mazzo due volte invertendo chi comincia.
//
//   ./match <mazzi> <thread> <reteA.onnx> <reteB.onnx>
//
// Serve per decidere se una nuova rete e' davvero meglio della precedente
// prima di metterla in produzione.
#include "ai.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

std::mutex out_mtx;

// Selezione a 1 semimossa, identica a quella del sito (js/game/game.js):
// si prendono le 3 mosse col prior piu' alto, si applica ognuna, si valuta la
// posizione risultante e si sceglie il massimo di  value + prior * 0.3.
Move choose1Ply(NeuralAI& ai, Fast128 my, Fast128 opp, const std::vector<int>& hand) {
    std::vector<Move> legal = NeuralAI::legalMoves(my, opp, hand);
    if (legal.empty()) return {-1, -1, false};

    float policy[200];
    ai.evaluate(my, opp, hand, policy);

    std::sort(legal.begin(), legal.end(), [&](const Move& a, const Move& b) {
        return policy[a.pos + (a.is_removal ? 100 : 0)] > policy[b.pos + (b.is_removal ? 100 : 0)];
    });
    if (legal.size() > 3) legal.resize(3);
    if (legal.size() == 1) return legal[0];

    float best = -1e9f;
    Move chosen = legal[0];
    for (const Move& m : legal) {
        Fast128 simMy = my, simOpp = opp;
        if (m.is_removal) clearBit(simOpp, m.pos); else setBit(simMy, m.pos);

        std::vector<int> simHand = hand;
        simHand.erase(simHand.begin() + m.card_idx_in_hand);

        float prior = policy[m.pos + (m.is_removal ? 100 : 0)];
        float score = ai.evaluate(simMy, simOpp, simHand) + prior * 0.3f;
        if (score > best) { best = score; chosen = m; }
    }
    return chosen;
}

// Ritorna +1 se vince chi muove per primo, -1 se vince l'altro, 0 patta.
bool USE_1PLY = true;  // true = come il sito; false = solo policy in argmax

int playGame(NeuralAI* first, NeuralAI* second, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<int> deck;
    for (int i = 0; i < 52; i++) { deck.push_back(i); deck.push_back(i); }
    std::shuffle(deck.begin(), deck.end(), rng);

    Fast128 b1 = {0,0}, b2 = {0,0};
    std::vector<int> h1, h2;
    for (int i = 0; i < 7; ++i) {
        h1.push_back(deck.back()); deck.pop_back();
        h2.push_back(deck.back()); deck.pop_back();
    }

    bool firstTurn = true;
    int ply = 0;
    while (true) {
        Fast128& mine = firstTurn ? b1 : b2;
        Fast128& theirs = firstTurn ? b2 : b1;
        std::vector<int>& hand = firstTurn ? h1 : h2;
        NeuralAI* ai = firstTurn ? first : second;

        Move mv = USE_1PLY ? choose1Ply(*ai, mine, theirs, hand)
                           : ai->computeBestMove(mine, theirs, hand, ply);
        if (mv.pos == -1) return 0;

        if (mv.is_removal) clearBit(theirs, mv.pos);
        else setBit(mine, mv.pos);

        hand.erase(hand.begin() + mv.card_idx_in_hand);
        if (!deck.empty()) { hand.push_back(deck.back()); deck.pop_back(); }

        if (SequenceLogic::checkWin(mine)) return firstTurn ? 1 : -1;

        firstTurn = !firstTurn;
        ply++;
        if (ply > 400) return 0;
    }
}

int main(int argc, char** argv) {
    int PAIRS = (argc >= 2) ? std::stoi(argv[1]) : 50;
    int THREADS = (argc >= 3) ? std::stoi(argv[2]) : 4;
    std::string PATH_A = argv[3], PATH_B = argv[4];
    if (argc >= 6) USE_1PLY = (std::string(argv[5]) != "policy");

    initGameConstants();

    std::atomic<int> winA{0}, winB{0}, draws{0};
    std::atomic<int> aFirstWins{0}, bFirstWins{0};

    auto worker = [&](int tid, int pairs) {
        NeuralAI aiA(PATH_A, true, 1000 + tid);   // greedy: valutazione, non self-play
        NeuralAI aiB(PATH_B, true, 2000 + tid);
        for (int i = 0; i < pairs; i++) {
            uint32_t seed = 90000u + tid * 100000u + i;
            // Stesso mazzo giocato due volte, invertendo chi comincia
            int r1 = playGame(&aiA, &aiB, seed);   // A muove per primo
            if (r1 == 1) { winA++; aFirstWins++; } else if (r1 == -1) winB++; else draws++;

            int r2 = playGame(&aiB, &aiA, seed);   // B muove per primo
            if (r2 == 1) { winB++; bFirstWins++; } else if (r2 == -1) winA++; else draws++;
        }
    };

    std::vector<std::thread> ts;
    int per = PAIRS / THREADS, rem = PAIRS % THREADS;
    for (int t = 0; t < THREADS; t++) ts.emplace_back(worker, t, per + (t < rem ? 1 : 0));
    for (auto& t : ts) t.join();

    int tot = winA + winB + draws;
    printf("\n=== %d partite (%d mazzi, ognuno giocato nei due sensi) ===\n", tot, PAIRS);
    printf("  A = %s\n  B = %s\n\n", PATH_A.c_str(), PATH_B.c_str());
    printf("  A vince : %4d  (%.1f%%)\n", winA.load(), 100.0 * winA / tot);
    printf("  B vince : %4d  (%.1f%%)\n", winB.load(), 100.0 * winB / tot);
    printf("  patte   : %4d  (%.1f%%)\n", draws.load(), 100.0 * draws / tot);
    printf("\n  vittorie di chi muove per primo: A %d/%d, B %d/%d\n",
           aFirstWins.load(), PAIRS, bFirstWins.load(), PAIRS);
    return 0;
}
