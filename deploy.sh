#!/usr/bin/env bash
#
# Pubblica il sito sullo spazio web dell'Unige, montato via SFTP con GVFS.
#
#   ./deploy.sh          pubblica
#   ./deploy.sh --dry    mostra cosa farebbe, senza toccare niente
#
# Il server non e' Docker ma XAMPP/PHP-FPM, e la document root e' public_html.
# Il file .env NON viene pubblicato: sta nella home del server, un livello
# sopra public_html, dove il web server non puo' servirlo (api/core/env.php lo
# cerca prima li' e poi nella document root).

set -euo pipefail

MOUNT="/run/user/$(id -u)/gvfs/sftp:host=saw.dibris.unige.it/home/s6313243"
DEST="$MOUNT/public_html"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DRY=""
[ "${1:-}" = "--dry" ] && DRY="--dry-run"

if [ ! -d "$DEST" ]; then
    echo "Errore: $DEST non e' raggiungibile."
    echo "Apri la cartella SFTP nel file manager per montarla, poi riprova."
    exit 1
fi

if [ ! -f "$MOUNT/.env" ]; then
    echo "Attenzione: manca $MOUNT/.env sul server."
    echo "Senza quel file il sito non si collega al database."
    echo "Copia .env.example, riempilo e mettilo li' (NON dentro public_html)."
    exit 1
fi

echo "Da:  $SRC"
echo "A:   $DEST"
echo

# --delete tiene il server allineato al repository: i file rimossi qui
# spariscono anche la'. Le esclusioni proteggono cio' che non va pubblicato.
rsync -rlt --no-perms --no-owner --no-group --delete $DRY \
    --exclude '.git/' \
    --exclude '.gitignore' \
    --exclude '.env' \
    --exclude '.env.example' \
    --exclude 'deploy.sh' \
    --exclude 'initdb' \
    --exclude 'README.md' \
    --exclude 'sequence_net/' \
    --exclude '*.swp' \
    --exclude '.DS_Store' \
    --itemize-changes \
    "$SRC/" "$DEST/"

echo
if [ -n "$DRY" ]; then
    echo "Prova a vuoto: non e' stato modificato niente."
else
    echo "Pubblicato: https://saw.dibris.unige.it/~s6313243/"
fi
