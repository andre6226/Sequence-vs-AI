// =====================================================================
// Motore di Sequence per il browser, compilato in WebAssembly.
//
// Sostituisce il vecchio Monte Carlo piatto con un MCTS (mcts.hpp) e non
// dipende da nessuna libreria esterna: niente ONNX, niente rete neurale.
// La pagina non deve piu' scaricare il modello da 1,4 MB ne' il runtime
// ONNX dalla CDN.
//
// Compilazione (dalla radice del progetto):
//   source ~/emsdk/emsdk_env.sh
//   em++ -O3 -std=c++17 js/game/sequence_wasm.cpp -o js/game/sequence.js \
//        -I sequence_net/datagen --bind \
//        -s MODULARIZE=1 -s EXPORT_NAME=createSequenceModule \
//        -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT=web -s DYNAMIC_EXECUTION=0
//
// DYNAMIC_EXECUTION=0 e' obbligatorio: senza, la glue di Emscripten usa eval
// e la Content-Security-Policy del sito la blocca. Il gioco resta muto, con
// un solo EvalError in console.
//
// Interfaccia verso il JavaScript, nella stessa forma di prima:
//   const Module = await createSequenceModule();
//   const ai = new Module.SequenceEngine();
//   const grid = new Module.VectorInt();       // 100 caselle: 0 vuota, 1 umano, 2 AI
//   const hand = new Module.VectorString();    // "10C", "JD", ...
//   const mv = ai.computeBestMove(grid, hand, 200, oppHandSize, deckSize);
//   mv.pos / mv.card_idx / mv.is_removal
//   ai.winRate()   // 0..1, stima di vittoria alla radice
// =====================================================================

#include "mcts.hpp"
#include <emscripten/bind.h>
#include <string>
#include <vector>

#include "heuristic_priors.hpp"

// ---------------------------------------------------------------------
class SequenceEngine {
public:
    SequenceEngine() : m_mcts(defaultConfig(), heuristicPriors) {
        initGameConstants();
        m_mcts.seed((uint64_t)this ^ 0x5EC0FFEEull);
    }

    // flatGrid: 100 valori, 0 vuota / 1 umano / 2 AI (come nello stato del gioco)
    // handStr : la mano dell'AI, nomi di carta
    // budgetMs: millisecondi di ricerca
    // oppHandSize / deckSize: quante carte ha l'avversario e quante restano
    emscripten::val computeBestMove(const std::vector<int>& flatGrid,
                                    const std::vector<std::string>& handStr,
                                    int budgetMs, int oppHandSize, int deckSize)
    {
        Fast128 my = {0, 0}, opp = {0, 0};
        if (flatGrid.size() == 100) {
            for (int i = 0; i < 100; i++) {
                if (flatGrid[i] == 2)      setBit(my, i);
                else if (flatGrid[i] == 1) setBit(opp, i);
            }
        }

        std::vector<int> hand;
        hand.reserve(handStr.size());
        for (const std::string& c : handStr) {
            int id = CardTranslator::toId(c);
            if (id >= 0) hand.push_back(id);
        }

        MctsConfig cfg = defaultConfig();
        cfg.budget_ms = (budgetMs > 0) ? (double)budgetMs : 200.0;
        m_mcts.setConfig(cfg);

        MctsStats st;
        Move mv = m_mcts.search(my, opp, hand,
                                (size_t)std::max(0, oppHandSize),
                                (size_t)std::max(0, deckSize), &st);
        m_lastWinRate  = st.win_rate;
        m_lastSims     = st.simulations;

        emscripten::val r = emscripten::val::object();
        r.set("pos", mv.pos);
        r.set("card_idx", mv.card_idx_in_hand);
        r.set("is_removal", mv.is_removal);
        return r;
    }

    // Stima di vittoria dell'ultima ricerca, da 0 a 1. Non e' una previsione
    // di una rete: e' la frazione di simulazioni vinte, misurata.
    double winRate() const { return m_lastWinRate; }
    int    simulations() const { return (int)m_lastSims; }

private:
    static MctsConfig defaultConfig() {
        MctsConfig c;
        c.budget_ms   = 200.0;
        c.c_puct      = 1.6;
        c.prior_visits = 48;
        // In WebAssembly la memoria costa: un albero piu' contenuto basta,
        // visto che il tempo per mossa e' comunque una frazione di secondo.
        c.max_nodes   = 120000;
        return c;
    }

    Mcts   m_mcts;
    double m_lastWinRate = 0.5;
    long   m_lastSims = 0;
};

EMSCRIPTEN_BINDINGS(sequence_module) {
    using namespace emscripten;
    class_<SequenceEngine>("SequenceEngine")
        .constructor<>()
        .function("computeBestMove", &SequenceEngine::computeBestMove)
        .function("winRate", &SequenceEngine::winRate)
        .function("simulations", &SequenceEngine::simulations);
    register_vector<int>("VectorInt");
    register_vector<std::string>("VectorString");
}
