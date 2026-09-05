export class AppManager {
    constructor(ui, api) {
        this.ui = ui;
        this.api = api;
        this.user = null;
        this.activeChatId = null;
    }

    async init() {
        try {
            const res = await this.api.getProfile();
            if (res.success) {
                this.user = res.data;
                this.ui.updateNav(true);
                if (location.pathname.includes('profile.php')) this.ui.renderProfile(this.user);

                // CHAT
                if (location.pathname.includes('chat.php')) {
                    await this.loadUserList(); 
                    //polling ogni 2 secondi
                    setInterval(() => {
                        if (this.activeChatId) this.loadChat(false);
                    }, 2000);

                }

            } else { throw new Error("Guest"); }
        } catch (e) {
            this.ui.updateNav(false);
            if (['profile.php', 'chat.php', 'game.php'].some(p => location.pathname.includes(p))) window.location.href = 'login.php';
        }
        if (location.pathname.includes('leaderboard.php')) this.loadLeaderboard();
    }

    async loadUserList() {
        const res = await this.api.getUserList();
        if (res.success) {
            this.ui.renderUserList(res.data, this.activeChatId, (userClicked) => {
                this.switchChat(userClicked);
            });
        }
    }

    async switchChat(partner) {
        this.activeChatId = partner.id;
        //aggiorno per colorare di verde
        await this.loadUserList();
        this.ui.setChatActive(true, partner.username);
        await this.loadChat(true);
    }

    async loadChat(forcescroll = false) {
        if (!this.activeChatId) return;
        const res = await this.api.getChatMessages(this.activeChatId);
        if (res.success) {
            this.ui.renderChat(res.data, forcescroll);
        }
    }

    async handleSendMessage(msg) {
        if (!this.activeChatId) return;
        const res = await this.api.sendChatMessage(this.activeChatId, msg);
        if (res.success) this.loadChat(true);
        else alert("Invio fallito");
    }

    navigateTo(page) {
        const pages = {
            login: 'login.php',
            profile: 'profile.php',
            leaderboard: 'leaderboard.php',
            chat: 'chat.php',
            game: 'game.php',
            home: 'index.php'
        };
        if (pages[page]) {
            location.href = pages[page];
        }
    }

    async handleLogin(data) {
        const result = await this.api.login(data.email, data.password);
        if (result.success) {
            this.user = result.user;
            this.ui.updateNav(true);
            this.navigateTo('home');
        } else {
            alert("login fallito: ");
        }
    }

    async handleRegister(data) {
        if (!data.password || !this._validatePassword(data.password)) return;
        if (data.password !== data.confirm_password) {
            alert("Le password non coincidono");
            return;
        }

        const result = await this.api.register(data);
        if (result.success) {
            this.navigateTo('home');
        } else {
            alert("registrazione fallita");
        }
    }

    async handleLogout() {
        await this.api.logout();
        this.ui.updateNav(false);
        this.navigateTo('login');
    }

    async handleProfileUpdate(data) {
        const result = await this.api.updateProfile(data);
        if (result.success) {
            this.user = result.data;
            this.ui.renderProfile(this.user);
        } else {
            alert("aggiornamento profilo fallito");
        }
    }

    async handlePasswordChange(oldPassword, newPassword) {
        if (!this._validatePassword(newPassword)) return false;

        const result = await this.api.changePassword(oldPassword, newPassword);
        if (result.success) {
            return true;
        } else {
            alert("Cambio password fallito");
            return false;
        }
    }

    async loadLeaderboard() {
        const result = await this.api.getLeaderboard();
        if (result.success) {
            this.ui.renderLeaderboard(result.data);
        }
    }



    _validatePassword(password) {
        if (password.length < 8) {
            alert("La password deve essere lunga almeno 8 caratteri");
            return false;
        }

        const hasLetter = /[A-Za-z]/.test(password);
        const hasNumber = /[0-9]/.test(password);
        const hasSpecial = /[^A-Za-z0-9]/.test(password);

        if (!hasLetter || !hasNumber || !hasSpecial) {
            alert("La password deve contenere almeno una lettera, un numero e un carattere speciale");
            return false;
        }
        return true;
    }
}