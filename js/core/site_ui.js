export class SiteUI {
    constructor(callbacks) {
        this.cb = callbacks;
        this.els = {
            // ... Elementi esistenti ...
            logoutBtn: document.getElementById('btn-logout'),
            navAuth: document.querySelectorAll('.auth-only'),
            navGuest: document.querySelectorAll('.guest-only'),
            loginBox: document.getElementById('login-box'),
            registerBox: document.getElementById('register-box'),
            forms: { login: document.getElementById('form-login'), register: document.getElementById('form-register') },
            toggles: document.querySelectorAll('.form-toggle-text a'),
            profileForm: document.getElementById('profile-form'),
            profileDisplay: { name: document.getElementById('profile-name'), badge: document.getElementById('profile-badge') },
            pwdModal: { overlay: document.getElementById('password-modal'), openBtn: document.getElementById('change-password-btn'), closeBtn: document.getElementById('close-pwd-btn'), form: document.getElementById('password-form') },
            leaderboardBody: document.getElementById('leaderboard-body'),

            // --- CHAT (Aggiornati con i nuovi ID dell'HTML) ---
            chatList: document.getElementById('chat-user-list'),
            chatMsgs: document.getElementById('chat-messages'),
            chatInput: document.getElementById('chat-input'),
            chatBtn: document.getElementById('chat-btn'),
            chatHeader: document.getElementById('chat-header'),
            chatPartnerName: document.getElementById('chat-partner-name')
        };
        this._initListeners();
    }

    _initListeners() {
        this._bindClick(this.els.logoutBtn, () => this.cb.onLogout());
        this._bindSubmit(this.els.forms.login, this.cb.onLoginSubmit);
        this._bindSubmit(this.els.forms.register, this.cb.onRegisterSubmit);
        if(this.els.toggles.length) this.els.toggles.forEach(l => this._bindClick(l, () => this._toggleLoginRegister()));
        this._bindSubmit(this.els.profileForm, this.cb.onProfileUpdate);
        
        // Modal cambio password
        this._bindClick(this.els.pwdModal.openBtn, () => this.els.pwdModal.overlay.classList.remove('hidden'));
        this._bindClick(this.els.pwdModal.closeBtn, () => this._closeModal());
        this._bindSubmit(this.els.pwdModal.form, async (d) => { if(await this.cb.onPasswordChange(d.old_password, d.new_password)) this._closeModal(); });

        // Chat
        this._bindClick(this.els.chatBtn, () => this._handleChatSend());
        if(this.els.chatInput) this.els.chatInput.addEventListener('keypress', (e) => { if(e.key === 'Enter') this._handleChatSend(); });
    }

    renderUserList(users, activeId, onUserClick) {
        if (!this.els.chatList) return;
        this.els.chatList.innerHTML = '';

        users.forEach(u => {
            const div = document.createElement('div');
            div.className = 'available-user'; 
            div.textContent = u.username;
            
            if (u.id == activeId) {
                div.style.backgroundColor = '#82e1857b';
                div.style.color = 'white';
            }
            div.style.cursor = 'pointer';

            this._bindClick(div, () => onUserClick(u));
            this.els.chatList.appendChild(div);
        });
    }

    renderChat(msgs, forceScroll = false) {
        //per togliere eventuali caratteri HTML
        const decode = s => new DOMParser().parseFromString(s || '', "text/html").body.textContent;
        if (!this.els.chatMsgs) return;
        const container = this.els.chatMsgs;
        container.innerHTML = '';

        if (msgs.length === 0) {
            container.innerHTML = '<div class="msg">Nessun messaggio qui.</div>';
            return;
        }

        msgs.forEach(m => {
            const div = document.createElement('div');
            div.className = m.is_me ? 'msg sent' : 'msg'; 
            
            const strong = document.createElement('strong');
            strong.textContent = decode(m.is_me ? 'Tu' : m.mittente_nome) + ': ';
            div.append(strong, decode(m.messaggio));
            
            container.appendChild(div);
        });
        if (forceScroll) {
            this.scrolldownChat();
        }

    }

    scrolldownChat() {
        if (!this.els.chatMsgs) return;

        setTimeout(() => {
            this.els.chatMsgs.scrollTo({
                top: this.els.chatMsgs.scrollHeight,
                behavior: 'smooth' 
            });
        }, 100);
    }

    // attiva/sisattiva input chat
    setChatActive(isActive, partnerName = '') {
        if(this.els.chatInput) this.els.chatInput.disabled = !isActive;
        if(this.els.chatBtn) this.els.chatBtn.disabled = !isActive;
        
        if(isActive && this.els.chatHeader) {
            this.els.chatHeader.style.display = 'block';
            if(this.els.chatPartnerName) this.els.chatPartnerName.textContent = partnerName;
        }
    }

    _handleChatSend() {
        const txt = this.els.chatInput.value.trim();
        if(txt) {
            this.cb.onChatSend(txt);
            this.els.chatInput.value = '';
        }
    }

    _closeModal() {
        this.els.pwdModal.overlay.classList.add('hidden');
        this.els.pwdModal.form.reset();
    }

    _toggleLoginRegister() {
        this.els.loginBox.classList.toggle('hidden');
        this.els.registerBox.classList.toggle('hidden');
    }

    updateNav(isLoggedIn) {
        this.els.navAuth.forEach(el => el.style.display = isLoggedIn ? 'inline' : 'none');
        this.els.navGuest.forEach(el => el.style.display = isLoggedIn ? 'none' : 'inline');
    }

    renderProfile(user) {
        if (!this.els.profileForm) return;

        const fieldMap = {
            email: 'email',
            nome: 'nome',
            cognome: 'cognome',
            citta: 'citta',
            bio: 'bio',
            livello: 'livello'
        };

        Object.keys(fieldMap).forEach(key => {
            const fieldName = fieldMap[key];
            if (this.els.profileForm.elements[fieldName] && user[key]) {
                this.els.profileForm.elements[fieldName].value = user[key];
            }
        });

        if (this.els.profileDisplay.name) 
            this.els.profileDisplay.name.textContent = user.username;
        
        if (this.els.profileDisplay.badge) 
            this.els.profileDisplay.badge.textContent = user.livello;
        
    }

renderLeaderboard(data) {
        const tbody = this.els.leaderboardBody;
        if (!tbody) return;

        tbody.innerHTML = '';

        data.forEach((user, index) => {
            const row = tbody.insertRow();
            
            row.insertCell().textContent = index + 1;
            row.insertCell().textContent = user.username;
            row.insertCell().textContent = user.livello;
            row.insertCell().textContent = user.vittorie;
            row.insertCell().textContent = `${user.win_rate}%`;
        });
    }

    _bindClick(element, callback) {
        if (element) {
            element.addEventListener('click', (event) => {
                event.preventDefault();
                callback();
            });
        }
    }

    _bindSubmit(form, callback) {
        if (form) {
            form.addEventListener('submit', (event) => {
                event.preventDefault();
                const formData = Object.fromEntries(new FormData(form).entries());
                callback(formData);
            });
        }
    }
}