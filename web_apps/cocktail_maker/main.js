import { DecentScale } from './modules/scale.js';
import { UIController } from './modules/ui-controller.js';
// import { TimerManager } from './modules/timer.js';
// import { Led } from './modules/led.js';
import { SQLManager } from './modules/sqlite.js';

document.addEventListener('DOMContentLoaded', async () => {
    // Initialize components in correct order
    // const timerManager = new TimerManager();
    // const ui = new UIController(timerManager);
    // timerManager.setUIController(ui);
    // const scale = new DecentScale(ui, timerManager);
    // const led = new Led(scale);
    const sqlManager = new SQLManager();
    await sqlManager.initDatabase();
    const ui = new UIController();
    const allCocktailsContentDiv = document.getElementById('allCocktailsContent');
    const searchInput = document.getElementById('searchInput');
    const searchButton = document.getElementById('searchButton');
    const searchResultsDiv = document.getElementById('searchResults');
    const searchResultsContentDiv = document.getElementById('searchResultsContent');
    const allCocktails = sqlManager.getAllCocktails();
    ui.renderCocktailList(allCocktails, allCocktailsContentDiv);

    const allCocktailsDiv = document.getElementById('allCocktails');
    const backToAllButton = document.getElementById('backToAllButton');
    //seach cocktail function 
    function handleSearch() {
        const searchTerm = searchInput.value.toLowerCase().trim();
        if (searchTerm === "") {
            searchResultsDiv.classList.add('hidden');
            allCocktailsDiv.classList.remove('hidden');
            ui.renderCocktailList(sqlManager.getAllCocktails(), allCocktailsContentDiv);
        } else {
            const filteredCocktails = sqlManager.searchCocktails(searchTerm);
            ui.renderCocktailList(filteredCocktails, searchResultsContentDiv);
            searchResultsDiv.classList.remove('hidden');
            allCocktailsDiv.classList.add('hidden');
        }
    }

    // Initial display
    ui.renderCocktailList(sqlManager.getAllCocktails(), allCocktailsContentDiv);
    // displayCocktails(cocktailsData, allCocktailsContentDiv);

    searchButton.addEventListener('click', handleSearch);
    searchInput.addEventListener('keypress', (event) => {
        if (event.key === 'Enter') handleSearch();
    });
    backToAllButton.addEventListener('click', () => {
        searchInput.value = '';
        handleSearch();
    });
    //toggle read more 
    allCocktailsContentDiv.addEventListener('click', (event) => {
        ui.toggleIngredientsVisibility(event);
    });
    searchResultsContentDiv.addEventListener('click', (event) => {
        ui.toggleIngredientsVisibility(event);
    });
    // Connect button
    document.getElementById('connect')?.addEventListener('click', async () => {
        if (scale.activeConnection) {
            await scale.disconnect();
        } else {
            const connectionType = document.getElementById('connectionType').value;
            await scale.connect(connectionType);
        }
    });
    const executeButton = document.getElementById('execute-button');
    if (executeButton) {
        executeButton.addEventListener('click', () => sqlManager.executeQuery());
    }
    // Timer control
    document.getElementById('toggleTimer')?.addEventListener('click', () => {
        ui.toggleTimer();
    });

    // Other controls
    document.getElementById('tareButton')?.addEventListener('click', () => scale.tare());
    document.getElementById('toggleLed')?.addEventListener('click', () => led.toggleLed());
    document.getElementById('exportCSV')?.addEventListener('click', () => scale.exportToCSV());
    document.getElementById('exportJSON')?.addEventListener('click', () => scale.exportToJSON());
});