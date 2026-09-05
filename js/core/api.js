export class ApiClient {
    constructor() {
        this.baseUrl = './api';
    }

    async login(email, password) {
        const formData = new FormData();
        formData.append('email', email);
        formData.append('password', password);

        const response = await fetch(`${this.baseUrl}/auth/login.php`, {
            method: 'POST',
            body: formData
        });
        return await response.json();
    }

    async register(userData) {
        const formData = new FormData();
        for (const key in userData) {
            formData.append(key, userData[key]);
        }
        const response = await fetch(`${this.baseUrl}/auth/register.php`, {
            method: 'POST',
            body: formData
        });
        return await response.json();
    }

    async logout() {
        const response = await fetch(`${this.baseUrl}/auth/logout.php`);
        return await response.json();
    }

    async getProfile() {
        return await this._fetchWithAuth('/user/profile.php');
    }

    async updateProfile(data) {
        //URLSearchParams per inviare i dati in formato application/x-www-form-urlencoded perchè non devo popolare POST ma fare una PUT (se disponibile)
        const params = new URLSearchParams();
        for (const key in data) {
            params.append(key, data[key]);
        }

        return await this._fetchWithAuth('/user/profile.php', {
            method: 'POST',
            body: params.toString(),
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
        });
    }

    async changePassword(oldPassword, newPassword) {
        const formData = new FormData();
        formData.append('old_password', oldPassword);
        formData.append('new_password', newPassword);

        return await this._fetchWithAuth('/user/change_password.php', {
            method: 'POST',
            body: formData
        });
    }

    async getLeaderboard() {
        return await this._fetchWithAuth('/user/leaderboard.php');
    }

    async sendGameResult(outcome) {
        const formData = new FormData();
        formData.append('outcome', outcome);
        
        return await this._fetchWithAuth('/game/result.php', {
            method: 'POST',
            body: formData
        });
    }
    async getUserList() {
        return await this._fetchWithAuth('/user/list.php');
    }

    async getChatMessages(partnerId) {
        if (!partnerId) return { success: true, data: [] };
        return await this._fetchWithAuth(`/chat/messages.php?partner_id=${partnerId}`);
    }

    async sendChatMessage(destinatarioId, messaggio) {
        const f = new FormData(); 
        f.append('destinatario_id', destinatarioId); 
        f.append('messaggio', messaggio);
        return await this._fetchWithAuth('/chat/messages.php', { method: 'POST', body: f });
    }
    
    async _fetchWithAuth(endpoint, options = {}) {
        options.credentials = 'include'; 
        
        const response = await fetch(`${this.baseUrl}${endpoint}`, options);


        return await response.json();
    }
}
