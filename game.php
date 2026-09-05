<?php include 'header.php'; ?>
<link rel="stylesheet" href="css/game.css">

<div class="game-layout">
    <div id="board" class="board-grid"></div>

    <div class="game-sidebar">
        
        <div class="panel">
            <h3>La tua Mano 🃏</h3>
            <div id="hand-container" class="hand-grid"></div>
            <button class="btn restart-btn">Restart</button>

        </div>

		<div class="panel">
            <h3>Probabilità di Vittoria</h3>
            <div class="score-row">
                <span>Tu: <b id="win-p1" class="score-val">50%</b></span>
                <span>AI: <b id="win-ai" class="score-val">50%</b></span>
            </div>
        </div>

    </div>
</div>

<div id="modal-overlay" class="hidden">
    <div class="panel text-center">
        <h2 id="game-result-title">PARTITA FINITA</h2>
        <p id="game-result-msg">...</p>
        <button class="btn restart-btn">Gioca Ancora</button>
    </div>
</div>

<script src="https://cdn.jsdelivr.net/npm/onnxruntime-web/dist/ort.min.js"></script>
<script type="module">
    import { initGame } from './js/game/gamemain.js';
    initGame().catch(console.error);
</script>

<?php include 'footer.php'; ?>
