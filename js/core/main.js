import { ApiClient } from './api.js';
import { SiteUI } from './site_ui.js';
import { AppManager } from './app_manager.js';

export function initApp() {
    const api = new ApiClient();
    let app = null; 

    const ui = new SiteUI({
        onLoginSubmit: (data) => app.handleLogin(data),
        onRegisterSubmit: (data) => app.handleRegister(data),
        onProfileUpdate: (data) => app.handleProfileUpdate(data),
        onChatSend: (msg) => app.handleSendMessage(msg),
        onLogout: () => app.handleLogout(),
        onNavClick: (page) => app.navigateTo(page),
        onPasswordChange: (oldP, newP) => app.handlePasswordChange(oldP, newP) 
    });

    app = new AppManager(ui, api);
    app.init();
}

document.addEventListener('DOMContentLoaded', initApp);