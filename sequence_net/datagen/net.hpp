#pragma once
// =====================================================================
// SequenceNetV2, inferenza scritta a mano.
//
// Niente ONNX Runtime: la rete e' un tronco convoluzionale fisso su una
// scacchiera 10x10, quindi le dimensioni sono note a compile time e il
// grafo non serve a niente.
//
// Formato NHWC: i canali sono contigui, cosi' il ciclo piu' interno gira
// sui canali di uscita ed e' vettorizzabile (SIMD128 in WebAssembly).
//
// Il passaggio in avanti e' spezzato in due, e non e' un dettaglio:
//
//   trunk()     dipende SOLO dalla scacchiera. E' il 99,7% del calcolo.
//               Produce policy[200] e vvec[100]: 1200 byte da mettere
//               in una transposition table, chiave = la scacchiera.
//
//   valueTail() prende vvec + la mano e fa gli ultimi due Gemm.
//               19.584 MAC, un millecinquecentesimo del totale.
//
// Cosi' rivalutare la stessa scacchiera con una mano ricampionata costa
// meno di un rollout casuale.
// =====================================================================

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace snet {

constexpr int BH = 10, BW = 10, NP = BH * BW;   // 100 celle
constexpr int CT = 64;                          // canali del tronco
constexpr int NPOL = 200, NVV = NP, NHAND = 52;

// ---- conv 3x3, padding 1, NHWC ----
template <int CI, int CO>
static void conv3x3(const float* in, const float* wt, const float* bias, float* out) {
    for (int y = 0; y < BH; y++) {
        for (int x = 0; x < BW; x++) {
            float acc[CO];
            for (int co = 0; co < CO; co++) acc[co] = bias[co];
            for (int ky = 0; ky < 3; ky++) {
                const int iy = y + ky - 1;
                if (iy < 0 || iy >= BH) continue;
                for (int kx = 0; kx < 3; kx++) {
                    const int ix = x + kx - 1;
                    if (ix < 0 || ix >= BW) continue;
                    const float* ip = in + (iy * BW + ix) * CI;
                    const float* wp = wt + (ky * 3 + kx) * CI * CO;
                    for (int ci = 0; ci < CI; ci++) {
                        const float a = ip[ci];
                        const float* wr = wp + ci * CO;
                        for (int co = 0; co < CO; co++) acc[co] += a * wr[co];
                    }
                }
            }
            float* op = out + (y * BW + x) * CO;
            for (int co = 0; co < CO; co++) op[co] = acc[co];
        }
    }
}

// ---- conv 1x1, NHWC, pesi [ci][co] ----
template <int CI, int CO>
static void conv1x1(const float* in, const float* wt, const float* bias, float* out) {
    for (int p = 0; p < NP; p++) {
        const float* ip = in + p * CI;
        float* op = out + p * CO;
        for (int co = 0; co < CO; co++) op[co] = bias[co];
        for (int ci = 0; ci < CI; ci++) {
            const float a = ip[ci];
            const float* wr = wt + ci * CO;
            for (int co = 0; co < CO; co++) op[co] += a * wr[co];
        }
    }
}

// ---- Gemm con transB=1: out[o] = bias[o] + somma_i in[i] * w[o*NI + i] ----
static void gemm(const float* in, const float* w, const float* bias,
                 float* out, int NI, int NO) {
    for (int o = 0; o < NO; o++) {
        const float* wr = w + (size_t)o * NI;
        float s = bias[o];
        for (int i = 0; i < NI; i++) s += in[i] * wr[i];
        out[o] = s;
    }
}

static inline void relu(float* v, int n) {
    for (int i = 0; i < n; i++) if (v[i] < 0.0f) v[i] = 0.0f;
}

// Quello che si mette in cache: dipende dalla sola scacchiera.
struct BoardCache {
    float policy[NPOL];
    float vvec[NVV];
};

class Net {
public:
    bool load(const char* path) {
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long bytes = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        m_blob.resize(bytes / sizeof(float));
        size_t got = std::fread(m_blob.data(), sizeof(float), m_blob.size(), f);
        std::fclose(f);
        return got == m_blob.size() && bind();
    }

    bool loadFromMemory(const float* data, size_t n) {
        m_blob.assign(data, data + n);
        return bind();
    }

    // board: NHWC, 100 celle x 3 piani. 99,7% del costo sta qui.
    void trunk(const float* board, BoardCache& out) const {
        static thread_local float a[NP * CT], b[NP * CT], c[NP * CT];

        conv3x3<3, CT>(board, w_in, b_in, a);
        relu(a, NP * CT);

        for (int i = 0; i < 4; i++) {
            conv3x3<CT, CT>(a, w_b[i][0], b_b[i][0], b);
            relu(b, NP * CT);
            conv3x3<CT, CT>(b, w_b[i][1], b_b[i][1], c);
            for (int k = 0; k < NP * CT; k++) a[k] = c[k] + a[k];
            relu(a, NP * CT);
        }

        // testa policy: conv 1x1 a 2 canali, poi riordino in NCHW perche'
        // il Reshape dell'export appiattisce canale per canale.
        float pc[NP * 2];
        conv1x1<CT, 2>(a, w_pc, b_pc, pc);
        relu(pc, NP * 2);
        float flat[NPOL];
        for (int p = 0; p < NP; p++) { flat[p] = pc[p * 2]; flat[NP + p] = pc[p * 2 + 1]; }
        gemm(flat, w_pf, b_pf, out.policy, NPOL, NPOL);

        // testa value, parte che dipende dalla scacchiera: un solo canale
        conv1x1<CT, 1>(a, w_vc, b_vc, out.vvec);
        relu(out.vvec, NVV);
    }

    // hand: 52 valori. Questo e' il pezzo che si rifa' a ogni rivisita.
    float valueTail(const float* vvec, const float* hand) const {
        float cat[NVV + NHAND];
        std::memcpy(cat, vvec, NVV * sizeof(float));
        std::memcpy(cat + NVV, hand, NHAND * sizeof(float));
        float h[128];
        gemm(cat, w_v1, b_v1, h, NVV + NHAND, 128);
        relu(h, 128);
        float o;
        gemm(h, w_v2, b_v2, &o, 128, 1);
        return std::tanh(o);
    }

    void forward(const float* board, const float* hand,
                 float* policy_out, float* value_out) const {
        BoardCache bc;
        trunk(board, bc);
        std::memcpy(policy_out, bc.policy, NPOL * sizeof(float));
        *value_out = valueTail(bc.vvec, hand);
    }

private:
    bool bind() {
        const float* p = m_blob.data();
        const float* end = p + m_blob.size();
        auto take = [&](size_t n) { const float* r = p; p += n; return r; };

        w_in = take(3 * 3 * 3 * CT);  b_in = take(CT);
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 2; j++) {
                w_b[i][j] = take(3 * 3 * CT * CT);
                b_b[i][j] = take(CT);
            }
        w_pc = take(CT * 2);   b_pc = take(2);
        w_pf = take(NPOL * NPOL); b_pf = take(NPOL);
        w_vc = take(CT * 1);   b_vc = take(1);
        w_v1 = take(128 * (NVV + NHAND)); b_v1 = take(128);
        w_v2 = take(1 * 128);  b_v2 = take(1);
        return p == end;
    }

    std::vector<float> m_blob;
    const float *w_in, *b_in;
    const float *w_b[4][2], *b_b[4][2];
    const float *w_pc, *b_pc, *w_pf, *b_pf;
    const float *w_vc, *b_vc, *w_v1, *b_v1, *w_v2, *b_v2;
};

}  // namespace snet
