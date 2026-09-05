<?php include 'header.php'; ?>
<div class="panel profile-container">
    <div class="text-center">
        <div class="profile-avatar">👤</div>
        <h2 id="profile-name">Nome Utente</h2>
        <span id="profile-badge">Esperto</span>
    </div>
    
    <form id="profile-form" class="profile-form">
        <label>Email</label>
        <input name="email" type="email" disabled class="disabled-input">
        
        <label>Nome</label>
        <input name="nome" type="text" placeholder="Il tuo nome">
        <label>Cognome</label>
        <input name="cognome" type="text" placeholder="Il tuo cognome">

        <label>Città</label>
        <input name="citta" type="text" placeholder="La tua città">
        
        <label>About Me</label>
        <textarea name="bio" rows="3"></textarea>

          <label for="livello">Scegli il tuo livello</label>
            <select name="livello">
                <option value="Base">Base</option>
                <option value="Intermedio">Intermedio</option>
                <option value="Esperto">Esperto</option>
            </select>
  <br><br>
<button type="submit" class="btn-save">Salva Modifiche</button>

        <button type="button" id="change-password-btn" class="btn warning">Cambia password </button> 

    </form>
</div>
<div id="password-modal" class="modal-overlay hidden">
    <div class="panel modal-content">
        <h3>Cambia Password 🔐</h3>
        <form id="password-form">
            <label>Vecchia Password</label>
            <input type="password" name="old_password" required placeholder="Inserisci quella attuale">
            
            <label>Nuova Password</label>
            <input type="password" name="new_password" required placeholder="Min 8 caratteri, 1 numero, 1 simbolo">
            
            <div class="modal-buttons">
                <button type="button" id="close-pwd-btn" class="btn warning">Annulla</button>
                <button type="submit" class="btn">Conferma Cambio</button>
            </div>
        </form>
    </div>
</div>
<?php include 'footer.php'; ?>