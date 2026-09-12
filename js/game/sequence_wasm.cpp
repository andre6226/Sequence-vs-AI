// =====================================================================
// Motore di Sequence per il browser, compilato in WebAssembly.
//
// Un MCTS (mcts.hpp) guidato dai prior di SequenceNetV2. La rete gira qui
// dentro, con l'inferenza scritta a mano di net.hpp: niente ONNX Runtime,
// niente CDN. La pagina scarica i pesi (weights.bin, 1,4 MB) e li passa al
// motore; se il file manca il gioco continua con i prior euristici.
//
// La rete ordina le mosse e basta. Valutare con lei anche le foglie
// costerebbe 1,74 ms l'una contro 0,025 ms di una simulazione: sarebbero
// 115 foglie invece di 8.000. Misurato, non stimato.
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
//   ai.loadWeights(new Float32Array(buf));    // opzionale: attiva la rete
//   const grid = new Module.VectorInt();       // 100 caselle: 0 vuota, 1 umano, 2 AI
//   const hand = new Module.VectorString();    // "10C", "JD", ...
//   const mv = ai.computeBestMove(grid, hand, 200, oppHandSize, deckSize);
//   mv.pos / mv.card_idx / mv.is_removal
//   ai.winRate()   // 0..1, frazione di simulazioni vinte, non un output della rete
//   ai.usingNet()  // true se i prior vengono dalla rete
// =====================================================================

#include "mcts.hpp"
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <vector>

#include "heuristic_priors.hpp"
#include "net_priors.hpp"

// ---------------------------------------------------------------------
class SequenceEngine {
public:
    SequenceEngine() : m_cfg(defaultConfig()), m_mcts(m_cfg, heuristicPriors) {
        initGameConstants();
        m_mcts.seed((uint64_t)this ^ 0x5EC0FFEEull);
    }

    // Riceve weights.bin come Float32Array. Da qui in poi i prior arrivano
    // dalla rete. Se fallisce non succede niente di male: restano gli
    // euristici, e il gioco e' identico a prima.
    bool loadWeights(emscripten::val bytes) {
        std::vector<float> w = emscripten::convertJSArrayToNumberVector<float>(bytes);
        if (w.empty() || !m_net.loadFromMemory(w.data(), w.size())) return false;
        m_usingNet = true;
        m_cfg.prior_visits = NET_PRIOR_VISITS;
        m_mcts.setConfig(m_cfg);
        m_mcts.setPriors([this](Fast128 my, Fast128 opp, const std::vector<int>& hand,
                                const std::vector<Move>& moves, std::vector<float>& out) {
            return netPriors(m_net, my, opp, hand, moves, out);
        });
        return true;
    }

    bool usingNet() const { return m_usingNet; }

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

        m_cfg.budget_ms = (budgetMs > 0) ? (double)budgetMs : 200.0;
        m_mcts.setConfig(m_cfg);

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
        c.c_puct      = 2.5;
        c.prior_visits = 48;     // con i prior euristici, che costano zero
        // In WebAssembly la memoria costa: un albero piu' contenuto basta,
        // visto che il tempo per mossa e' comunque una frazione di secondo.
        c.max_nodes   = 120000;
        return c;
    }

    // Quanti nodi pagano la rete. Molto piu' alto della soglia euristica:
    // in WebAssembly una valutazione costa 1,74 ms, e a soglia 48 la rete si
    // mangerebbe meta' del budget. A 300 la paga poco piu' della radice.
    static const int NET_PRIOR_VISITS = 300;

    MctsConfig m_cfg;
    Mcts       m_mcts;
    snet::Net  m_net;
    bool       m_usingNet = false;
    double     m_lastWinRate = 0.5;
    long       m_lastSims = 0;
};

EMSCRIPTEN_BINDINGS(sequence_module) {
    using namespace emscripten;
    class_<SequenceEngine>("SequenceEngine")
        .constructor<>()
        .function("loadWeights", &SequenceEngine::loadWeights)
        .function("usingNet", &SequenceEngine::usingNet)
        .function("computeBestMove", &SequenceEngine::computeBestMove)
        .function("winRate", &SequenceEngine::winRate)
        .function("simulations", &SequenceEngine::simulations);
    register_vector<int>("VectorInt");
    register_vector<std::string>("VectorString");
}
