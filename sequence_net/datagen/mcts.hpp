#pragma once
// =====================================================================
// MCTS per Sequence.
//
// Due vincoli hanno deciso la forma di questo codice, entrambi misurati:
//
//   valutazione della rete   0,470 ms   ->    426 in 0,2 s
//   rollout casuale completo 0,0013 ms  -> 151.000 in 0,2 s
//
// Una valutazione di rete costa circa 360 rollout. Metterla su ogni foglia,
// come in AlphaZero, lascerebbe un albero da poche centinaia di nodi. Qui la
// sorgente dei prior serve solo a ORDINARE le mosse, e le foglie si valutano
// giocando la partita fino in fondo, che e' quasi gratis.
//
// I prior arrivano da una funzione sostituibile (PriorFn), non dalla rete:
// cosi' lo stesso motore gira con SequenceNetV2, con un'euristica scritta a
// mano, o senza niente. E' anche il motivo per cui questo header include solo
// moves.hpp e NON ai.hpp: compila senza ONNX Runtime, per esempio quando
// finisce in WebAssembly.
//
// Informazione nascosta: la mano avversaria non si conosce. Si usa
// Information Set MCTS -- a ogni simulazione si ricampiona una mano
// plausibile, e nell'albero si contano separatamente le volte in cui una
// mossa era DISPONIBILE e quelle in cui e' stata SCELTA.
// =====================================================================

#include "moves.hpp"
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <functional>

// Riempie out[] con un punteggio per ciascuna mossa (non serve che sommino a
// 1, ci pensa il chiamante). false = nessun prior, si resta sull'uniforme.
using PriorFn = std::function<bool(Fast128 my, Fast128 opp,
                                   const std::vector<int>& hand,
                                   const std::vector<Move>& moves,
                                   std::vector<float>& out)>;

struct MctsConfig {
    double budget_ms      = 200.0;  // tempo per mossa
    double c_puct         = 1.6;    // quanto spingere verso i rami poco esplorati
    int    prior_visits   = 48;     // visite prima di chiedere i prior a un nodo interno
    int    max_ply        = 140;    // taglio di sicurezza nei rollout
    bool   rollout_greedy = true;   // nei rollout, chiudere se si puo' vincere subito
    size_t max_nodes      = 400000;
};

struct MctsStats {
    long   simulations = 0;
    long   prior_calls = 0;
    double elapsed_ms  = 0;
    double win_rate    = 0.5;   // stima di vittoria alla radice, da 0 a 1
};

class Mcts {
public:
    explicit Mcts(const MctsConfig& cfg, PriorFn priors = PriorFn())
        : m_cfg(cfg), m_priors(std::move(priors)), m_rng(0xC0FFEE) {}

    void seed(uint64_t s) { m_rng.seed((uint32_t)s); }
    void setConfig(const MctsConfig& c) { m_cfg = c; }
    void setPriors(PriorFn p) { m_priors = std::move(p); }

    // my/opp: bitboard dal punto di vista di chi deve muovere.
    // myHand: la mia mano (nota). oppHandSize/deckSize: quante carte hanno
    // l'avversario e il mazzo, l'unica cosa che se ne sa davvero.
    Move search(Fast128 my, Fast128 opp, const std::vector<int>& myHand,
                size_t oppHandSize, size_t deckSize, MctsStats* out = nullptr)
    {
        m_nodes.clear();
        m_nodes.push_back(Node());
        m_rootMy = my; m_rootOpp = opp; m_rootHand = myHand;
        m_oppHandSize = oppHandSize; m_deckSize = deckSize;

        std::vector<Move> rootMoves = legalMovesFor(my, opp, myHand);
        MctsStats st;
        if (rootMoves.empty()) { if (out) *out = st; return { -1, -1, false }; }

        // Chiusura immediata: nessuna ricerca serve
        for (const Move& m : rootMoves) {
            if (m.is_removal) continue;
            Fast128 t = my; setBit(t, m.pos);
            if (SequenceLogic::checkWin(t)) { st.win_rate = 1.0; if (out) *out = st; return m; }
        }
        if (rootMoves.size() == 1) { if (out) *out = st; return rootMoves[0]; }

        auto t0 = std::chrono::steady_clock::now();
        auto elapsed = [&]{
            return std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
        };

        // Il tempo si controlla a blocchi: chiamare l'orologio a ogni
        // simulazione costerebbe piu' della simulazione stessa.
        const int CHECK_EVERY = 64;
        while (true) {
            for (int i = 0; i < CHECK_EVERY; i++) { simulate(st); st.simulations++; }
            if (elapsed() >= m_cfg.budget_ms) break;
        }
        st.elapsed_ms = elapsed();

        // Si gioca la mossa piu' VISITATA, non quella col punteggio piu' alto:
        // un punteggio alto su poche visite e' rumore.
        int best = -1, bestVisits = -1;
        const Node& root = m_nodes[0];
        for (size_t k = 0; k < root.moves.size(); k++) {
            int ci = root.children[k];
            int v = (ci >= 0) ? m_nodes[ci].visits : 0;
            if (v > bestVisits) { bestVisits = v; best = (int)k; }
        }
        if (root.visits > 0) st.win_rate = (root.sum / root.visits + 1.0) / 2.0;
        if (out) *out = st;
        if (best < 0) return rootMoves[0];

        // Nell'albero le mosse sono solo casella + tipo, senza indice di carta.
        // Chi la gioca ha bisogno dell'indice vero: lo si ripesca qui.
        const Move& key = root.moves[best];
        for (const Move& rm : rootMoves)
            if (rm.pos == key.pos && rm.is_removal == key.is_removal) return rm;
        return rootMoves[0];
    }

private:
    struct Node {
        int    visits = 0;
        double sum = 0.0;                 // risultati sommati, visti dal giocatore alla radice
        bool   priorsDone = false;
        std::vector<Move>  moves;         // identita': casella + tipo, senza indice di carta
        std::vector<int>   children;      // -1 = non ancora creato
        std::vector<float> prior;
        std::vector<int>   avail;         // quante volte la mossa era disponibile
    };

    struct Det { Fast128 my, opp; std::vector<int> myHand, oppHand, deck; };

    MctsConfig m_cfg;
    PriorFn m_priors;
    std::mt19937 m_rng;
    std::vector<Node> m_nodes;
    Fast128 m_rootMy{0,0}, m_rootOpp{0,0};
    std::vector<int> m_rootHand;
    size_t m_oppHandSize = 0, m_deckSize = 0;

    // Ricampiona mano avversaria e mazzo fra le carte che non ho in mano.
    // Approssimazione nota: non si tiene conto di quali carte siano gia' state
    // giocate, perche' dalla scacchiera non si ricostruisce (una pedina puo'
    // venire da entrambe le copie, e i Jack non lasciano traccia).
    Det determinize() {
        Det d;
        d.my = m_rootMy; d.opp = m_rootOpp; d.myHand = m_rootHand;
        std::vector<int> unseen;
        unseen.reserve(104);
        for (int i = 0; i < 52; i++) { unseen.push_back(i); unseen.push_back(i); }
        for (int c : m_rootHand) {
            auto it = std::find(unseen.begin(), unseen.end(), c);
            if (it != unseen.end()) unseen.erase(it);
        }
        std::shuffle(unseen.begin(), unseen.end(), m_rng);
        size_t n = std::min(m_oppHandSize, unseen.size());
        d.oppHand.assign(unseen.begin(), unseen.begin() + n);
        d.deck.assign(unseen.begin() + n, unseen.end());
        if (d.deck.size() > m_deckSize) d.deck.resize(m_deckSize);
        return d;
    }

    void ensurePriors(int ni, Fast128 my, Fast128 opp,
                      const std::vector<int>& hand, MctsStats& st)
    {
        Node& n = m_nodes[ni];
        if (n.priorsDone || !m_priors) return;
        if (n.prior.size() != n.moves.size()) return;   // liste non ancora allineate

        std::vector<float> raw;
        if (!m_priors(my, opp, hand, n.moves, raw)) return;
        if (raw.size() != n.moves.size()) return;
        st.prior_calls++;

        float mx = -1e30f;
        for (float v : raw) mx = std::max(mx, v);
        float sum = 0.0f;
        for (size_t i = 0; i < raw.size(); i++) {
            raw[i] = expf((raw[i] - mx) / 4.0f);   // softmax morbida: serve un ordine, non una certezza
            sum += raw[i];
        }
        if (sum <= 0.0f) return;
        for (size_t i = 0; i < raw.size(); i++) n.prior[i] = raw[i] / sum;
        n.priorsDone = true;
    }

    // Allinea le liste del nodo alle mosse di QUESTA determinizzazione.
    // L'identita' di una mossa NON puo' includere l'indice della carta in mano:
    // la mano avversaria si rigenera a ogni simulazione, quindi quell'indice
    // vale solo per la determinizzazione che l'ha prodotto.
    void syncMoves(int ni, const std::vector<Move>& legal, std::vector<int>& idx) {
        Node& n = m_nodes[ni];
        idx.clear();
        for (const Move& lm : legal) {
            int found = -1;
            for (size_t i = 0; i < n.moves.size(); i++)
                if (n.moves[i].pos == lm.pos && n.moves[i].is_removal == lm.is_removal) { found = (int)i; break; }
            if (found < 0) {
                Move key = lm; key.card_idx_in_hand = -1;
                n.moves.push_back(key);
                n.children.push_back(-1);
                n.prior.push_back(1.0f / (float)std::max<size_t>(1, legal.size()));
                n.avail.push_back(0);
                found = (int)n.moves.size() - 1;
            }
            idx.push_back(found);
        }
    }

    int  newNode()  { m_nodes.push_back(Node()); return (int)m_nodes.size() - 1; }
    bool treeFull() const { return m_nodes.size() >= m_cfg.max_nodes; }

    void simulate(MctsStats& st) {
        Det d = determinize();
        bool myTurn = true;
        int node = 0;
        std::vector<int> path{ 0 };
        std::vector<int> idx;
        int result = 0;
        bool decided = false;

        for (int depth = 0; depth < m_cfg.max_ply; depth++) {
            Fast128& mine   = myTurn ? d.my : d.opp;
            Fast128& theirs = myTurn ? d.opp : d.my;
            std::vector<int>& hand = myTurn ? d.myHand : d.oppHand;

            std::vector<Move> legal = legalMovesFor(mine, theirs, hand);
            if (legal.empty()) { result = 0; decided = true; break; }

            syncMoves(node, legal, idx);
            for (int i : idx) m_nodes[node].avail[i]++;

            // La radice i prior li ha sempre; gli altri nodi solo quando sono
            // abbastanza frequentati da giustificarne il costo. Va fatto DOPO
            // syncMoves, che e' cio' che crea le liste del nodo.
            if (!m_nodes[node].priorsDone
                && (node == 0 || m_nodes[node].visits >= m_cfg.prior_visits))
                ensurePriors(node, mine, theirs, hand, st);

            int pick = -1;
            for (int i : idx) if (m_nodes[node].children[i] < 0) { pick = i; break; }

            bool expanding = (pick >= 0) && !treeFull();
            if (pick >= 0 && treeFull()) {
                int alt = -1;
                for (int i : idx) if (m_nodes[node].children[i] >= 0) { alt = i; break; }
                if (alt < 0) { result = rollout(d, myTurn); decided = true; break; }
                pick = alt;
            }

            if (pick < 0) {
                double bestScore = -1e18;
                double sqrtN = std::sqrt((double)std::max(1, m_nodes[node].visits));
                for (int i : idx) {
                    const Node& ch = m_nodes[m_nodes[node].children[i]];
                    double q = (ch.visits > 0) ? (ch.sum / ch.visits) : 0.0;
                    if (!myTurn) q = -q;                   // l'avversario vuole l'opposto
                    double u = m_cfg.c_puct * m_nodes[node].prior[i] * sqrtN / (1.0 + ch.visits);
                    double score = q + u;
                    if (score > bestScore) { bestScore = score; pick = i; }
                }
            }

            // Ripesca la mossa concreta: e' lei che porta l'indice valido
            // della carta da scartare in QUESTA determinizzazione.
            Move mv = m_nodes[node].moves[pick];
            for (size_t j = 0; j < legal.size(); j++)
                if (idx[j] == pick) { mv = legal[j]; break; }
            if (mv.card_idx_in_hand < 0 || (size_t)mv.card_idx_in_hand >= hand.size()) {
                result = 0; decided = true; break;         // non dovrebbe accadere
            }

            if (mv.is_removal) clearBit(theirs, mv.pos); else setBit(mine, mv.pos);
            hand.erase(hand.begin() + mv.card_idx_in_hand);
            if (!d.deck.empty()) { hand.push_back(d.deck.back()); d.deck.pop_back(); }

            if (expanding) {
                int ci = newNode();
                m_nodes[node].children[pick] = ci;
                path.push_back(ci);
                if (SequenceLogic::checkWin(mine)) result = myTurn ? 1 : -1;
                else                               result = rollout(d, !myTurn);
                decided = true;
                break;
            }

            path.push_back(m_nodes[node].children[pick]);
            node = m_nodes[node].children[pick];

            if (SequenceLogic::checkWin(mine)) { result = myTurn ? 1 : -1; decided = true; break; }
            myTurn = !myTurn;
        }

        if (!decided) result = 0;
        for (int ni : path) { m_nodes[ni].visits++; m_nodes[ni].sum += result; }
    }

    // Partita casuale fino alla fine. Con rollout_greedy, se una mossa chiude
    // la partita la si gioca: costa poco e rende le simulazioni molto meno
    // insensate del caso puro.
    int rollout(Det d, bool myTurn) {
        for (int p = 0; p < m_cfg.max_ply; p++) {
            Fast128& mine   = myTurn ? d.my : d.opp;
            Fast128& theirs = myTurn ? d.opp : d.my;
            std::vector<int>& hand = myTurn ? d.myHand : d.oppHand;

            std::vector<Move> legal = legalMovesFor(mine, theirs, hand);
            if (legal.empty()) return 0;

            int choice = -1;
            if (m_cfg.rollout_greedy) {
                for (size_t i = 0; i < legal.size(); i++) {
                    if (legal[i].is_removal) continue;
                    Fast128 t = mine; setBit(t, legal[i].pos);
                    if (SequenceLogic::checkWin(t)) { choice = (int)i; break; }
                }
            }
            if (choice < 0) choice = (int)(m_rng() % legal.size());

            const Move& m = legal[choice];
            if (m.is_removal) clearBit(theirs, m.pos); else setBit(mine, m.pos);
            hand.erase(hand.begin() + m.card_idx_in_hand);
            if (!d.deck.empty()) { hand.push_back(d.deck.back()); d.deck.pop_back(); }

            if (SequenceLogic::checkWin(mine)) return myTurn ? 1 : -1;
            myTurn = !myTurn;
        }
        return 0;
    }
};
