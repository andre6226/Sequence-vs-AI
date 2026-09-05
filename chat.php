<?php include 'header.php'; ?>

<div class="chat-box">
    <div id="chat-user-list" class="chat-list panel">
        <h4>Caricamento...</h4>
        </div>

    <div class="panel chat-panel">
        
        <div id="chat-header">
            Chat con: <b id="chat-partner-name">...</b>
        </div>

        <div class="chat-msgs" id="chat-messages">
            <div class="msg">👈 Seleziona un utente per iniziare</div>
        </div>
        
        <div class="chat-input-area">
            <input type="text" id="chat-input" placeholder="Scrivi un messaggio..." disabled>
            <button id="chat-btn" class="btn" disabled>📩</button>
        </div>
    </div>
</div>

<?php include 'footer.php'; ?>