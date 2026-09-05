# Sequence vs AI

Il gioco da tavolo Sequence contro una rete neurale che gira interamente nel
browser: rete addestrata per self-play, esportata in ONNX ed eseguita
client-side. Il backend PHP gestisce account, chat e classifica.

## Cosa c'è

- **Gioco** — scacchiera 10×10, mano di 7 carte, Jack jolly e Jack di rimozione.
  Vince chi completa due sequenze da 5 pedine.
- **AI** — ResNet convoluzionale con teste policy e value, valutata a ogni mossa
  nel browser. Mostra la probabilità di vittoria aggiornata in tempo reale.
- **Account** — registrazione, profilo, cambio password; sessione via JWT in
  cookie HttpOnly.
- **Chat** fra giocatori e **classifica** per win rate.

## Stack

PHP 8.2 + Apache, MariaDB, JavaScript a moduli ES senza framework.
Regole e stato della partita in JavaScript; la rete viene eseguita con
ONNX Runtime Web.

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

## Struttura

```
api/                 endpoint JSON
  core/              env, database, JWT, helper condivisi
  auth/ user/ chat/ game/
js/core/             client API, UI, routing
js/game/             regole, stato della partita, rendering
sequence_net/        pipeline di training (notebook Colab + datagen C++)
sequence_net.onnx    rete caricata dal browser
```

## Training

In `sequence_net/`: la rete gioca contro sé stessa (`datagen`, C++ multi-thread
con ONNX Runtime), i record diventano un dataset, training PyTorch su policy e
value, riesportazione in ONNX, si ripete. Una rete nuova sostituisce quella in
uso solo se la batte in un torneo diretto (`datagen/match.cpp`). Il notebook è
pensato per Colab e rileva da solo se girare su Drive o in locale.
`recover_weights.py` ricostruisce un checkpoint PyTorch da un `.onnx`, se il
`.pth` è andato perso.

[Grafo completo della rete (export Netron)](sequence_net/architettura.png)
