<?php include 'header.php'; ?>

<div class="panel login-container">
    
    <div id="login-box">
        <h2>Accedi 🔐</h2>
        <form id="form-login">
            <input type="email" name="email" placeholder="Email" required>
            <input type="password" name="password" placeholder="Password" required>
            <button type="submit" class="btn">Login</button>
        </form>
        <p class="form-toggle-text">
            Non hai un account? <a href="#">Registrati</a>
        </p>
    </div>

    <div id="register-box" class="hidden">
        <h2>Registrati 📝</h2>
        <form id="form-register">
            <input type="text" name="username" placeholder="Username" required>
            
            <input type="email" name="email" placeholder="Email" required>
            
            <input type="password" name="password" placeholder="Password (min 8 car)" required>
            <input type="password" name="confirm_password" placeholder="Conferma Password" required>
            <input type="text" name="nome" placeholder="Nome" required>
            <input type="text" name="cognome" placeholder="Cognome" required>

            <button type="submit" class="btn">Crea Account</button>
        </form>
        <p class="form-toggle-text">
            Hai già un account? <a href="#">Accedi</a>
        </p>
    </div>

</div>

<?php include 'footer.php'; ?>