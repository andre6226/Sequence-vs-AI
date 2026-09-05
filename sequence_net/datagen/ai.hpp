#pragma once
#include "board.hpp"
#include <vector>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <onnxruntime_cxx_api.h>

struct FastRNG {
    uint64_t s;
    FastRNG(uint64_t seed) : s(seed + 1) {}
    FORCE_INLINE uint32_t next() {
        uint64_t x=s; x^=x<<13; x^=x>>7; x^=x<<17; s=x; return (uint32_t)x;
    }
    FORCE_INLINE int nextInt(int max) { return (int)(((uint64_t)next() * (uint64_t)max) >> 32); }
    FORCE_INLINE float nextFloat() { return (float)(next() >> 8) / 16777216.0f; } // [0,1)
};

class NeuralAI {
    Ort::Env env;
    Ort::SessionOptions session_options;
    Ort::Session* session;
    Ort::MemoryInfo memory_info;
    FastRNG m_rng;
    bool m_greedy;

public:
    // model_path: file .onnx.  greedy = true disattiva l'esplorazione: si usa
    // per le partite di valutazione, dove serve la forza massima della rete.
    // Per il self-play va lasciato false, altrimenti le partite diventano
    // deterministiche e il dataset perde ogni varieta'.
    NeuralAI(const std::string& model_path, bool greedy = false, uint64_t seed = 0)
        : env(ORT_LOGGING_LEVEL_WARNING, "SequenceV2"),
          memory_info(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
          m_rng(seed ? seed : ((uint64_t)this ^ 1337)),
          m_greedy(greedy)
    { 
        initGameConstants(); 
        
        // Ottimizzazioni per far andare ONNX al massimo su singola CPU
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        
        // Carica il modello in RAM
        session = new Ort::Session(env, model_path.c_str(), session_options);
    }

    ~NeuralAI() {
        delete session;
    }

    // Genera le mosse legali per la mano data. Stessa logica usata da
    // computeBestMove, esposta perche' serve anche al lookahead a 1 semimossa.
    static std::vector<Move> legalMoves(Fast128 my, Fast128 opp, const std::vector<int>& hand) {
        Fast128 occupied = my | opp | MASK_CORNERS;
        Fast128 opp_locked = SequenceLogic::getLockedMask(opp);
        std::vector<Move> out;
        out.reserve(50);
        for (size_t i = 0; i < hand.size(); i++) {
            int cardID = hand[i];
            if (cardID == -1) continue;
            Fast128 mv = {0,0};
            bool is_rem = false;
            if (CardTranslator::isTwoEyedJack(cardID))      mv = (~occupied & MASK_BOARD);
            else if (CardTranslator::isOneEyedJack(cardID)) { mv = (opp & ~opp_locked); is_rem = true; }
            else                                            mv = (CARD_MAP[cardID] & ~occupied);
            Fast128 t = mv;
            while (!t.isZero()) out.push_back({(int)i, BitScanner::next(t), is_rem});
        }
        return out;
    }

    // Valuta una posizione: scrive i 200 logit della policy in policy_out
    // (se non nullo) e restituisce la value in [-1, 1], dal punto di vista
    // di chi possiede 'my'.
    float evaluate(Fast128 my, Fast128 opp, const std::vector<int>& hand, float* policy_out = nullptr) {
        Fast128 occupied = my | opp | MASK_CORNERS;
        Fast128 opp_locked = SequenceLogic::getLockedMask(opp);
        Fast128 playable_mask = {0,0};
        for (int cardID : hand) {
            if (cardID == -1) continue;
            if (CardTranslator::isTwoEyedJack(cardID))      playable_mask |= (~occupied & MASK_BOARD);
            else if (CardTranslator::isOneEyedJack(cardID)) playable_mask |= (opp & ~opp_locked);
            else                                            playable_mask |= (CARD_MAP[cardID] & ~occupied);
        }

        std::vector<float> board_tensor(300, 0.0f), hand_tensor(52, 0.0f);
        for (int i = 0; i < 100; i++) {
            uint64_t bit = (i < 64) ? (1ULL << i) : (1ULL << (i - 64));
            uint64_t m = (i < 64) ? my.lo : my.hi;
            uint64_t o = (i < 64) ? opp.lo : opp.hi;
            uint64_t p = (i < 64) ? playable_mask.lo : playable_mask.hi;
            if (m & bit) board_tensor[i] = 1.0f;
            if (o & bit) board_tensor[100 + i] = 1.0f;
            if (p & bit) board_tensor[200 + i] = 1.0f;
        }
        for (int c : hand) if (c >= 0 && c < 52) hand_tensor[c] += 1.0f;

        std::vector<int64_t> bs = {1,3,10,10}, hs = {1,52};
        Ort::Value bo = Ort::Value::CreateTensor<float>(memory_info, board_tensor.data(), 300, bs.data(), 4);
        Ort::Value ho = Ort::Value::CreateTensor<float>(memory_info, hand_tensor.data(), 52, hs.data(), 2);
        const char* in_names[] = {"board_input", "hand_input"};
        const char* out_names[] = {"policy_output", "value_output"};
        Ort::Value ins[] = {std::move(bo), std::move(ho)};
        auto outs = session->Run(Ort::RunOptions{nullptr}, in_names, ins, 2, out_names, 2);
        if (policy_out) std::memcpy(policy_out, outs[0].GetTensorMutableData<float>(), 200 * sizeof(float));
        return outs[1].GetTensorMutableData<float>()[0];
    }

    // Ricerca a 1 semimossa. Prende le TOP_K mosse col prior piu' alto, applica
    // ognuna, valuta la posizione risultante e le ordina per value + prior*0.3
    // (la stessa formula usata dal sito in js/game/game.js).
    //
    // Serve anche al self-play, non solo alla valutazione: se il dataset
    // registrasse la mossa campionata dalla policy, la rete verrebbe
    // addestrata sulle proprie stesse scelte e non avrebbe alcun modo di
    // migliorare. Registrando invece la mossa scelta dalla ricerca, la policy
    // impara a imitare qualcosa di piu' forte di se' stessa.
    //
    //  greedy = true  -> restituisce la migliore (valutazione, e sito)
    //  greedy = false -> campiona fra le candidate (self-play)
    static const int TOP_K = 3;

    // Peso del prior nel punteggio della ricerca: score = value + prior*PRIOR_W.
    // Va tenuto basso, altrimenti il prior schiaccia la value e il lookahead
    // non decide piu' niente (i due termini hanno scale molto diverse).
    float PRIOR_W = 0.30f;

    Move search1Ply(Fast128 my, Fast128 opp, const std::vector<int>& hand,
                    int ply = 0, bool greedy = true) {
        std::vector<Move> legal = legalMoves(my, opp, hand);
        if (legal.empty()) return {-1, -1, false};

        // Quota di mosse del tutto casuali, su TUTTE le legali: senza questa
        // il self-play resterebbe confinato alle prime tre della policy.
        if (!greedy) {
            int epsilon_chance = (ply < 8) ? 12 : 3;
            if (m_rng.nextInt(100) < epsilon_chance)
                return legal[m_rng.nextInt(legal.size())];
        }

        float policy[200];
        evaluate(my, opp, hand, policy);

        std::sort(legal.begin(), legal.end(), [&](const Move& a, const Move& b) {
            return policy[a.pos + (a.is_removal ? 100 : 0)] >
                   policy[b.pos + (b.is_removal ? 100 : 0)];
        });
        if ((int)legal.size() > TOP_K) legal.resize(TOP_K);
        if (legal.size() == 1) return legal[0];

        std::vector<float> scores(legal.size());
        for (size_t i = 0; i < legal.size(); i++) {
            const Move& m = legal[i];
            Fast128 simMy = my, simOpp = opp;
            if (m.is_removal) clearBit(simOpp, m.pos); else setBit(simMy, m.pos);
            std::vector<int> simHand = hand;
            simHand.erase(simHand.begin() + m.card_idx_in_hand);
            float prior = policy[m.pos + (m.is_removal ? 100 : 0)];
            scores[i] = evaluate(simMy, simOpp, simHand) + prior * PRIOR_W;
        }

        if (greedy) {
            size_t best = 0;
            for (size_t i = 1; i < scores.size(); i++) if (scores[i] > scores[best]) best = i;
            return legal[best];
        }

        // Campionamento softmax sui punteggi della ricerca (non sui logit grezzi)
        float temperature = (ply < 12) ? 0.35f : 0.20f;
        float mx = *std::max_element(scores.begin(), scores.end());
        float sum = 0.0f;
        for (size_t i = 0; i < scores.size(); i++) {
            scores[i] = expf((scores[i] - mx) / temperature);
            sum += scores[i];
        }
        float r = m_rng.nextFloat() * sum, acc = 0.0f;
        for (size_t i = 0; i < legal.size(); i++) {
            acc += scores[i];
            if (r <= acc) return legal[i];
        }
        return legal.back();
    }

  Move computeBestMove(Fast128 my, Fast128 opp, const std::vector<int>& hand, int ply = 0) {
            Fast128 occupied = my | opp | MASK_CORNERS;
            Fast128 opp_locked = SequenceLogic::getLockedMask(opp);
            Fast128 playable_mask = {0, 0};
            
            std::vector<Move> valid_moves;
            valid_moves.reserve(50);
    
            for(size_t i=0; i<hand.size(); i++) {
                int cardID = hand[i];
                if (cardID == -1) continue;
                
                Fast128 moves_for_this_card = {0,0};
                bool is_rem = false;
                if (CardTranslator::isTwoEyedJack(cardID)) {
                    moves_for_this_card = (~occupied & MASK_BOARD);
                } else if (CardTranslator::isOneEyedJack(cardID)) {
                    moves_for_this_card = (opp & ~opp_locked);
                    is_rem = true;
                } else {
                    moves_for_this_card = (CARD_MAP[cardID] & ~occupied);
                }
                playable_mask |= moves_for_this_card;
    
                Fast128 temp = moves_for_this_card;
                while(!temp.isZero()) {
                    valid_moves.push_back({(int)i, BitScanner::next(temp), is_rem});
                }
            }
    
            if (valid_moves.empty()) return {-1, -1, false};
    
            // Preparazione tensori per ONNX
            std::vector<float> board_tensor(300, 0.0f);
            std::vector<float> hand_tensor(52, 0.0f);
    
            for (int i = 0; i < 100; i++) {
                uint64_t bit = (i < 64) ? (1ULL << i) : (1ULL << (i - 64));
                if (i < 64) {
                    if (my.lo & bit) board_tensor[i] = 1.0f;
                    if (opp.lo & bit) board_tensor[100 + i] = 1.0f;
                    if (playable_mask.lo & bit) board_tensor[200 + i] = 1.0f;
                } else {
                    if (my.hi & bit) board_tensor[i] = 1.0f;
                    if (opp.hi & bit) board_tensor[100 + i] = 1.0f;
                    if (playable_mask.hi & bit) board_tensor[200 + i] = 1.0f;
                }
            }
    
            for (int c : hand) {
                if (c >= 0 && c < 52) hand_tensor[c] += 1.0f;
            }
    
            std::vector<int64_t> board_shape = {1, 3, 10, 10};
            std::vector<int64_t> hand_shape = {1, 52};
    
            Ort::Value board_ort = Ort::Value::CreateTensor<float>(memory_info, board_tensor.data(), 300, board_shape.data(), 4);
            Ort::Value hand_ort = Ort::Value::CreateTensor<float>(memory_info, hand_tensor.data(), 52, hand_shape.data(), 2);
    
            const char* input_names[] = {"board_input", "hand_input"};
            const char* output_names[] = {"policy_output", "value_output"};
            Ort::Value inputs[] = {std::move(board_ort), std::move(hand_ort)};
    
            auto output_tensors = session->Run(Ort::RunOptions{nullptr}, input_names, inputs, 2, output_names, 2);
            float* policy_arr = output_tensors[0].GetTensorMutableData<float>();
    
            // =================================================================
            // 4. ACTION MASKING & SELEZIONE DELLA MOSSA
            // =================================================================

            // Modalita' valutazione: argmax puro, nessun rumore.
            if (m_greedy) {
                float best_score = -1e9f;
                Move best_move = valid_moves[0];
                for (const auto& m : valid_moves) {
                    float score = policy_arr[m.pos + (m.is_removal ? 100 : 0)];
                    if (score > best_score) { best_score = score; best_move = m; }
                }
                return best_move;
            }

            // Self-play: campionamento softmax sui logit delle sole mosse legali.
            //
            // La versione precedente azzerava l'esplorazione dopo il turno 8
            // (epsilon 0, rumore 0.03 solo per i pareggi). Da li' in poi le
            // partite erano deterministiche: sempre le stesse linee, quindi la
            // rete le imparava a memoria e la value saturava, perche' in gioco
            // deterministico l'esito e' gia' deciso dalla mano iniziale.
            // La temperatura resta sopra zero per tutta la partita.
            // Valori scelti misurando i logit reali della rete: lo scarto fra
            // le mosse migliori e' circa 4.4, quindi T=0.6 lasciava alla prima
            // mossa quasi il 90% della probabilita'. Con questi la mossa top
            // pesa ~40% in apertura e ~63% dopo: varieta' vera, ma senza
            // scadere nel casuale (a T>=3 la policy non conta piu' nulla).
            float temperature = (ply < 12) ? 1.50f : 1.00f;

            // Piccola quota di mosse del tutto casuali, mai azzerata: garantisce
            // che anche le mosse a cui la policy da' probabilita' ~0 finiscano
            // ogni tanto nel dataset.
            int epsilon_chance = (ply < 8) ? 12 : 3;
            if (m_rng.nextInt(100) < epsilon_chance) {
                return valid_moves[m_rng.nextInt(valid_moves.size())];
            }

            float max_logit = -1e9f;
            for (const auto& m : valid_moves) {
                float l = policy_arr[m.pos + (m.is_removal ? 100 : 0)];
                if (l > max_logit) max_logit = l;
            }

            std::vector<float> probs(valid_moves.size());
            float sum = 0.0f;
            for (size_t i = 0; i < valid_moves.size(); i++) {
                const Move& m = valid_moves[i];
                float l = policy_arr[m.pos + (m.is_removal ? 100 : 0)];
                probs[i] = expf((l - max_logit) / temperature); // shift: evita overflow
                sum += probs[i];
            }

            float r = m_rng.nextFloat() * sum;
            float acc = 0.0f;
            for (size_t i = 0; i < valid_moves.size(); i++) {
                acc += probs[i];
                if (r <= acc) return valid_moves[i];
            }
            return valid_moves.back(); // raggiungibile solo per arrotondamenti
        }
};
