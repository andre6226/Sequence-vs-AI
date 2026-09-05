#pragma once
#include "board.hpp"
#include <vector>
#include <cstring>
#include <algorithm>
#include <onnxruntime_cxx_api.h>

struct FastRNG {
    uint64_t s;
    FastRNG(uint64_t seed) : s(seed + 1) {}
    FORCE_INLINE uint32_t next() {
        uint64_t x=s; x^=x<<13; x^=x>>7; x^=x<<17; s=x; return (uint32_t)x;
    }
    FORCE_INLINE int nextInt(int max) { return (int)(((uint64_t)next() * (uint64_t)max) >> 32); }
};

class NeuralAI {
    Ort::Env env;
    Ort::SessionOptions session_options;
    Ort::Session* session;
    Ort::MemoryInfo memory_info;
    FastRNG m_rng;

public:
    // Il costruttore ora prende il percorso del file .onnx
    NeuralAI(const std::string& model_path) 
        : env(ORT_LOGGING_LEVEL_WARNING, "SequenceV2"),
          memory_info(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
          m_rng((uint64_t)this ^ 1337) 
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
            // 4. ACTION MASKING & ESPLORAZIONE CONDIZIONATA (Decay)
            // =================================================================
            
            // 1. Mossa 100% casuale: 15% nei primi 8 turni, poi 0% per il resto della partita
            int epsilon_chance = (ply < 8) ? 15 : 0;
            if (m_rng.nextInt(100) < epsilon_chance) {
                int random_idx = m_rng.nextInt(valid_moves.size());
                return valid_moves[random_idx];
            }
    
            // 2. Rumore sui logits: 0.15f in apertura per differenziare, 0.03f nel mediogioco (tie-breaking)
            float noise_scale = (ply < 8) ? 0.15f : 0.03f;
    
            float best_score = -1e9;
            Move best_move = valid_moves[0];
    
            for (const auto& m : valid_moves) {
                int policy_idx = m.pos + (m.is_removal ? 100 : 0);
                float score = policy_arr[policy_idx];
                
                float noise = (m_rng.nextInt(1000) / 1000.0f) * noise_scale;
                score += noise;
    
                if (score > best_score) {
                    best_score = score;
                    best_move = m;
                }
            }
            return best_move;
        }
};
