import { UI } from './ui.js';
import { Game } from './game.js';

export async function initGame() {
    let gameInstance = null;

    const ui = new UI({ 
        onBoardClick: (i) => { if (gameInstance) gameInstance.clickBoard(i); }, 
        onHandClick:  (i) => { if (gameInstance) gameInstance.clickHand(i); },   
        onRestart:    (i) => { if (gameInstance) gameInstance.start(i); }
    });

    gameInstance = new Game(ui);
    await gameInstance.loadModel();
    gameInstance.start();
    
    console.log("Sequence Engine (Neural ONNX) caricato con successo!");
}
