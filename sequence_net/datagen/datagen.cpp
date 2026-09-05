#include <iostream>
#include <vector>
#include <fstream>
#include <random>
#include <algorithm>
#include <thread>
#include <mutex>
#include "ai.hpp" // Questo includerà automaticamente type.hpp e board.hpp

std::mutex console_mutex;

class Datagen {
    std::ofstream out_file;
    NeuralAI* ai_player; // <-- Ora è un puntatore alla rete neurale
    std::mt19937 rng;

public:
    Datagen(const std::string& filename, int seed_offset, const std::string& model_path) {
        out_file.open(filename, std::ios::binary | std::ios::out);
        rng.seed(1337 + seed_offset); 
        // Ogni worker ha la sua istanza della rete per evitare colli di bottiglia tra i thread
        ai_player = new NeuralAI(model_path);
    }

    ~Datagen() {
        if(out_file.is_open()) out_file.close();
        delete ai_player;
    }

void playSingleGame() {
        std::vector<int> deck = buildDeck();
        std::shuffle(deck.begin(), deck.end(), rng);

        Fast128 p1_board = {0,0};
        Fast128 p2_board = {0,0};
        std::vector<int> p1_hand;
        std::vector<int> p2_hand;

        for(int i=0; i<7; ++i) {
            p1_hand.push_back(deck.back()); deck.pop_back();
            p2_hand.push_back(deck.back()); deck.pop_back();
        }

        std::vector<GameRecord> game_history;
        bool p1_turn = true;
        int winner = 0;
        int ply = 0; // <--- CONTATORE DI MOSSE DELLA PARTITA

        while(true) {
            Fast128& current_board = p1_turn ? p1_board : p2_board;
            Fast128& opp_board     = p1_turn ? p2_board : p1_board;
            std::vector<int>& current_hand = p1_turn ? p1_hand : p2_hand;

            // Passiamo 'ply' per attivare l'esplorazione solo nei primi turni
            Move best_move = ai_player->computeBestMove(current_board, opp_board, current_hand, ply);
            
            if (best_move.pos == -1) { break; }

            // [Codice invariato di salvataggio record...]
            Fast128 occupied = current_board | opp_board | MASK_CORNERS;
            Fast128 opp_locked = SequenceLogic::getLockedMask(opp_board);
            Fast128 playable_mask = {0, 0};
            for (int cardID : current_hand) {
                if (cardID == -1) continue;
                if (CardTranslator::isTwoEyedJack(cardID)) {
                    playable_mask |= (~occupied & MASK_BOARD);
                } else if (CardTranslator::isOneEyedJack(cardID)) {
                    playable_mask |= (opp_board & ~opp_locked);
                } else {
                    playable_mask |= (CARD_MAP[cardID] & ~occupied);
                }
            }

            GameRecord record = {};
            record.my_board_lo = current_board.lo; record.my_board_hi = current_board.hi;
            record.opp_board_lo = opp_board.lo; record.opp_board_hi = opp_board.hi;
            record.playable_lo = playable_mask.lo; record.playable_hi = playable_mask.hi;
            record.hand_size = current_hand.size();
            for(size_t i = 0; i < current_hand.size(); i++) record.hand[i] = current_hand[i];
            record.move_pos = best_move.pos;
            record.is_removal = best_move.is_removal ? 1 : 0;
            
            game_history.push_back(record);

            if (best_move.is_removal) {
                clearBit(opp_board, best_move.pos);
            } else {
                setBit(current_board, best_move.pos);
            }

            current_hand.erase(current_hand.begin() + best_move.card_idx_in_hand);
            if (!deck.empty()) {
                current_hand.push_back(deck.back()); deck.pop_back();
            }

            if (SequenceLogic::checkWin(current_board)) {
                winner = p1_turn ? 1 : -1;
                break;
            }

            p1_turn = !p1_turn;
            ply++; // <--- INCREMENTA IL PLY
        }
        
        // [Salvataggio finale su file invariato...]
        for (size_t i = 0; i < game_history.size(); i++) {
            bool is_p1_record = (i % 2 == 0); 
            if (winner == 1)  game_history[i].result = is_p1_record ? 1 : -1;
            else if (winner == -1) game_history[i].result = is_p1_record ? -1 : 1;
            else game_history[i].result = 0;

            out_file.write(reinterpret_cast<const char*>(&game_history[i]), sizeof(GameRecord));
        }
    }

    std::vector<int> buildDeck() {
        std::vector<int> d;
        for(int i=0; i<52; i++) { d.push_back(i); d.push_back(i); }
        return d;
    }
};

void worker_task(int worker_id, int games_to_play, const std::string& model_path) {
    std::string filename = "dataset_worker_" + std::to_string(worker_id) + ".bin";
    Datagen dgen(filename, worker_id, model_path);
    
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "[Worker " << worker_id << "] Partito usando ONNX: " << model_path << "\n";
    }

    for(int i = 0; i < games_to_play; i++) {
        dgen.playSingleGame();
        
        if ((i + 1) % 50 == 0) {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << "[Worker " << worker_id << "] Completate " << (i+1) << "/" << games_to_play << "\n";
        }
    }
}

int main(int argc, char** argv) {
    int TOTAL_GAMES = 1000;
    int NUM_WORKERS = 4;
    std::string MODEL_PATH = "./sequence_net.onnx"; // <-- Metti il file ONNX nella stessa cartella dell'eseguibile!

    if (argc >= 2) TOTAL_GAMES = std::stoi(argv[1]);
    if (argc >= 3) NUM_WORKERS = std::stoi(argv[2]);
    if (argc >= 4) MODEL_PATH = argv[3];

    initGameConstants(); 
    
    std::cout << "Avvio SELF-PLAY NEURALE V2.\nPartite totali: " << TOTAL_GAMES 
              << " | Workers: " << NUM_WORKERS 
              << " | Modello: " << MODEL_PATH << "\n\n";

    int games_per_worker = TOTAL_GAMES / NUM_WORKERS;
    int remainder = TOTAL_GAMES % NUM_WORKERS;

    std::vector<std::thread> threads;

    for(int i = 0; i < NUM_WORKERS; i++) {
        int games_for_this_thread = games_per_worker + (i < remainder ? 1 : 0);
        threads.emplace_back(worker_task, i, games_for_this_thread, MODEL_PATH);
    }

    for(auto& t : threads) {
        t.join();
    }
    
    std::cout << "\nSelf-Play Neurale terminato con successo!" << std::endl;
    return 0;
}
