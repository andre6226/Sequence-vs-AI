# Sequence vs AI

Il gioco da tavolo Sequence contro un'intelligenza artificiale che gira
interamente nel browser: una ricerca ad albero compilata in WebAssembly, senza
niente da scaricare da servizi esterni. Il backend PHP gestisce account, chat e
classifica.

## Cosa c'è

- **Gioco** — scacchiera 10×10, mano di 7 carte, Jack jolly e Jack di rimozione.
  Vince chi completa due sequenze da 5 pedine.
- **AI** — MCTS con determinizzazione: a ogni mossa esplora migliaia di partite
  simulate, ricampionando le carte che l'avversario potrebbe avere. Circa 8.000
  simulazioni in 0,2 secondi nel browser, 13.500 compilato in nativo.
  Essendo informazione imperfetta, una mossa non è disponibile in tutte le
  simulazioni: la selezione la confronta solo con quelle in cui c'era davvero
  (Information Set MCTS).
- **Probabilità di vittoria** — la frazione di partite simulate che vinci. È una
  misura, non la previsione di un modello.
- **Account** — registrazione, profilo, cambio password; sessione via JWT in
  cookie HttpOnly.
- **Chat** fra giocatori e **classifica** per win rate.

## Stack

PHP 8.2 + Apache, MariaDB, JavaScript a moduli ES senza framework.
Regole e stato della partita in JavaScript; il motore di ricerca è C++
compilato in WebAssembly (93 KB in tutto), servito dal sito stesso.

## Avvio

Il repository è la document root del sito. Serve un container LAMP con
`mod_headers` attivo: l'`.htaccess` lo usa per gli header di sicurezza.

```bash
cp .env.example .env                              # poi riempilo
php -r "echo bin2hex(random_bytes(32));"          # per JWT_SECRET
mariadb -u root -p <nome_db> < initdb             # crea le tabelle
```

Le variabili d'ambiente del container hanno la precedenza sul `.env`, quindi in
produzione il file può non esistere.

Per giocare non serve compilare niente: `js/game/sequence.js` e
`js/game/sequence.wasm` sono già nel repository.

## Struttura

```
api/                       endpoint JSON
  core/                    env, database, JWT, helper condivisi
  auth/ user/ chat/ game/
js/core/                   client API, UI, routing
js/game/                   regole, stato della partita, rendering
  sequence_wasm.cpp        il motore per il browser
  sequence.js .wasm        il motore compilato, servito alla pagina
sequence_net/              pipeline di training e strumenti di confronto
sequence_net.onnx          rete SequenceNetV2: non serve piu' al sito,
                           resta come riferimento nei confronti
```

## Compilare

### Il motore del browser

Serve [Emscripten](https://emscripten.org/). Dalla radice del progetto:

```bash
source ~/emsdk/emsdk_env.sh
em++ -O3 -std=c++17 js/game/sequence_wasm.cpp -o js/game/sequence.js \
     -I sequence_net/datagen --bind \
     -s MODULARIZE=1 -s EXPORT_NAME=createSequenceModule \
     -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT=web \
     -s DYNAMIC_EXECUTION=0
```

`DYNAMIC_EXECUTION=0` non è facoltativo: senza, la glue generata da Emscripten
usa `eval`, che la Content-Security-Policy del sito blocca. Il gioco resterebbe
muto, con un solo `EvalError` in console.

Dopo ogni ricompilazione va incrementato il parametro di versione in
`game.php` (oggi `sequence.js?v=2`), altrimenti i browser continuano a servire
la copia che hanno in cache.

### Gli strumenti nativi

`datagen`, `match` e `mcts_match` girano sulla macchina, non nel browser, e
usano la rete neurale: per compilarli serve la **distribuzione C++ di ONNX
Runtime**, che non è nel repository perché pesa circa 16 MB.

Scaricala dalle [release ufficiali](https://github.com/microsoft/onnxruntime/releases)
nella versione `onnxruntime-linux-x64-1.18.0` e mettila in
`sequence_net/datagen/onnxruntime/` (il `.gitignore` la esclude già), oppure
tienila altrove e cambia i percorsi qui sotto:

```bash
ORT=sequence_net/datagen/onnxruntime

g++ -O2 -std=c++17 sequence_net/datagen/datagen.cpp -o datagen \
    -I sequence_net/datagen -I $ORT/include \
    -L $ORT/lib -Wl,-rpath,$ORT/lib -lonnxruntime -lpthread
```

Stessa riga per `match.cpp` e `mcts_match.cpp`. Il notebook di training se la
scarica da solo, non serve prepararla per Colab.

Il motore MCTS invece (`sequence_net/datagen/mcts.hpp`) **non** dipende da ONNX
Runtime: prende i prior da una funzione sostituibile, e nel browser usa
un'euristica scritta a mano. È il motivo per cui si può compilare in
WebAssembly senza trascinarsi dietro una libreria da 16 MB.

## Confrontare due versioni dell'AI

```bash
./mcts_match sequence_net.onnx <mazzi> <ms per mossa> <c_puct> <prior_visits> <net|heuristic|uniform>
./match <mazzi> <thread> <reteA.onnx> <reteB.onnx>
```

Ogni mazzo viene giocato due volte a colori invertiti. Servono alcune centinaia
di partite per distinguere differenze di pochi punti percentuali: su 120 partite
l'incertezza è già di circa 4,5 punti, e un risultato promettente a 1,5 sigma
sparisce quasi sempre quando lo si rimisura su mazzi nuovi.

Misure attuali, a 200 ms per mossa:

| avversario | risultato |
|---|---|
| SequenceNetV2 a 1 ply | 60% su 300 partite |
| motore euristico di un progetto Sequence esterno | 78% su 500 partite |

## Training della rete

In `sequence_net/`: la rete gioca contro sé stessa (`datagen`, C++
multi-thread), i record diventano un dataset, training PyTorch su policy e
value, riesportazione in ONNX, si ripete. Una rete nuova sostituisce quella in
uso solo se la batte in un torneo diretto. Il notebook è pensato per Colab e
rileva da solo se girare su Drive o in locale. `recover_weights.py` ricostruisce
un checkpoint PyTorch da un `.onnx`, se il `.pth` è andato perso.

La rete non è più l'avversario del gioco, ma resta il metro di paragone con cui
sono state misurate tutte le versioni del motore.

[Grafo completo della rete (export Netron)](sequence_net/architettura.png)
