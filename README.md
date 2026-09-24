# Sequence vs AI

Il gioco da tavolo Sequence contro un'AI che gira interamente nel browser:
ricerca ad albero in WebAssembly guidata da una rete neurale, senza niente da
scaricare da servizi esterni. Il backend PHP gestisce account, chat e classifica.

## Cosa c'è

- **Gioco** — scacchiera 10×10, mano di 7 carte, Jack jolly e Jack di rimozione.
  Vince chi completa due sequenze da 5 pedine.
- **AI** — Information Set MCTS: a ogni mossa simula migliaia di partite
  ricampionando le carte che l'avversario potrebbe avere. SequenceNetV2 ordina
  le mosse da esplorare, le foglie si valutano giocando fino in fondo. Circa
  6.800 simulazioni in 0,2 secondi.
- **Probabilità di vittoria** — la frazione di partite simulate che vinci. È una
  misura, non la previsione di un modello.
- **Account** con sessione JWT in cookie HttpOnly, **chat** e **classifica**.

## Stack

PHP 8.2 + Apache, MariaDB, JavaScript a moduli ES senza framework. Regole e
stato della partita in JavaScript; il motore è C++ compilato in WebAssembly
(100 KB) più i pesi della rete (1,4 MB), serviti dal sito stesso.

## Avvio

Il repository è la document root. Serve un container LAMP con `mod_headers`
attivo: l'`.htaccess` lo usa per gli header di sicurezza.

```bash
cp .env.example .env                              # poi riempilo
php -r "echo bin2hex(random_bytes(32));"          # per JWT_SECRET
mariadb -u root -p <nome_db> < initdb             # crea le tabelle
```

Le variabili d'ambiente del container hanno la precedenza sul `.env`, quindi in
produzione il file può non esistere.

Per giocare non serve compilare niente: `sequence.js`, `sequence.wasm` e
`weights.bin` sono già in `js/game/`.

## Struttura

```
api/core/                  env, database, JWT, helper condivisi
api/auth|user|chat|game/   endpoint JSON
js/core/                   client API, UI, routing
js/game/                   regole, stato, rendering, motore compilato
sequence_net/              training, strumenti di confronto, inferenza C++
sequence_net.onnx          SequenceNetV2, sorgente di verità dei pesi
```

## Compilare

### Il motore del browser

Serve [Emscripten](https://emscripten.org/).

```bash
source ~/emsdk/emsdk_env.sh
em++ -O3 -std=c++17 js/game/sequence_wasm.cpp -o js/game/sequence.js \
     -I sequence_net/datagen --bind \
     -s MODULARIZE=1 -s EXPORT_NAME=createSequenceModule \
     -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT=web \
     -s DYNAMIC_EXECUTION=0
```

Due cose non facoltative. `DYNAMIC_EXECUTION=0`: senza, la glue di Emscripten
usa `eval` e la Content-Security-Policy del sito la blocca, lasciando il gioco
muto con un solo `EvalError` in console. E dopo ogni ricompilazione va
incrementato il parametro di versione in `game.php` (oggi `sequence.js?v=3`),
altrimenti i browser servono la copia in cache.

I pesi si rigenerano dall'ONNX con `python3 sequence_net/export_weights.py`.
L'inferenza in `sequence_net/datagen/net.hpp` è scritta a mano e verificata
contro ONNX Runtime: nel browser non serve nessuna libreria.

### Gli strumenti nativi

`datagen`, `match` e `mcts_match` girano sulla macchina e usano ONNX Runtime,
che non è nel repository perché pesa 16 MB. Scarica
`onnxruntime-linux-x64-1.18.0` dalle
[release ufficiali](https://github.com/microsoft/onnxruntime/releases) in
`sequence_net/datagen/onnxruntime/` (già nel `.gitignore`), poi:

```bash
ORT=sequence_net/datagen/onnxruntime
g++ -O2 -std=c++17 sequence_net/datagen/datagen.cpp -o datagen \
    -I sequence_net/datagen -I $ORT/include \
    -L $ORT/lib -Wl,-rpath,$ORT/lib -lonnxruntime -lpthread
```

Stessa riga per `match.cpp` e `mcts_match.cpp`.

## Forza del motore

A 200 ms per mossa:

| avversario | risultato |
|---|---|
| SequenceNetV2 a 1 ply | 59% su 350 partite |
| motore euristico di un progetto Sequence esterno | 78% su 500 partite |

La rete serve i prior, non la valutazione delle foglie, per una ragione di
costo: una valutazione costa 1,74 ms contro 0,025 ms di una simulazione intera.
Sulle foglie ne resterebbero 115 invece di 6.800, e misurato così il motore
perde 38 a 62.

Il motore satura sopra i 200 ms: da 200 a 800 la forza non cambia. Il limite non
è la ricerca ma lo stimatore, che azzecca il vincitore nel 65% dei casi sia con
la rete sia con una media di rollout.

## Confrontare due versioni

```bash
./mcts_match sequence_net.onnx <mazzi> <ms per mossa> <c_puct> <prior_visits> <net|heuristic|uniform>
./match <mazzi> <thread> <reteA.onnx> <reteB.onnx>
```

Ogni mazzo si gioca due volte a colori invertiti. Servono alcune centinaia di
partite per vedere differenze di pochi punti: su 120 l'incertezza è già di 4,5
punti, e un risultato promettente a 1,5 sigma sparisce quasi sempre quando lo si
rimisura su mazzi nuovi.

## Training della rete

In `sequence_net/`: la rete gioca contro sé stessa (`datagen`), i record
diventano un dataset, training PyTorch su policy e value, riesportazione in
ONNX, si ripete. Una rete nuova sostituisce quella in uso solo se la batte in un
torneo diretto. Il notebook è pensato per Colab e rileva da solo se girare su
Drive o in locale. `recover_weights.py` ricostruisce un checkpoint PyTorch da un
`.onnx`, se il `.pth` è andato perso.

[Grafo completo della rete (export Netron)](sequence_net/architettura.png)

## Licenza

Distribuito con licenza MIT: vedi [LICENSE](LICENSE).
