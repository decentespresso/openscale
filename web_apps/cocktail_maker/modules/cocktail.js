import { SQLManager } from "./sqlite.js";
import { DecentScale } from './scale.js';
import { UIController } from './ui-controller.js';
import { SCALE_CONSTANTS } from "./constants.js";

//this version is for HDS 3.0.0 Wifi based only, connect logic is based on websocket. 
const ui = new UIController();
const scale = new DecentScale(ui, null);
const ws = new ReconnectingWebSocket(`ws://${window.location.host}/snapshot`);
scale.ws = ws;

const backToAllButton = document.getElementById('backToAllButton');
const user_added_weight = {};

//websocket connect logic 
ws.addEventListener('open', () => ui.updateStatus('WebSocket Connected'));
ws.addEventListener('close', () => ui.updateStatus('WebSocket Disconnected'));
ws.addEventListener('error', () => ui.updateStatus('WebSocket Error'));

ws.addEventListener('message', (event) => {
    const jsondata = JSON.parse(event.data);
    if (jsondata.grams !== undefined) {
        const weight = jsondata.grams;
        if (!isNaN(weight)) {
           scale.handleWebSocketWeight(weight);
       }
   }
});

function escapeHtml(unsafe) {
    if (typeof unsafe !== 'string') return '';
    return unsafe
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}

function mixtypeToSvg(mixtype) {
    if (!mixtype) return '';

    const svgPath = 'asset';
    let svgFile = '';

    switch (mixtype.toLowerCase()) {
        case 'add':   svgFile = 'build.svg'; break;
        case 'flame':   svgFile = 'flame.svg'; break;
        case 'float':   svgFile = 'float.svg'; break;
        case 'garnish': svgFile = 'garnish.svg'; break;
        case 'muddle':  svgFile = 'muddle.svg'; break;
        case 'rim':     svgFile = 'rim.svg'; break;
        case 'rinse':   svgFile = 'rinse2.svg'; break;
        case 'spritz':  svgFile = 'spritz.svg'; break;
        case 'top':     svgFile = 'top.svg'; break;
        case 'twist':   svgFile = 'twist.svg'; break;
        default:
            return escapeHtml(mixtype);
    }

    return `<img src="${svgPath}/${svgFile}" alt="${escapeHtml(mixtype)}" title="${escapeHtml(mixtype)}" style="height: 40px; width: 40px; display: inline-block; vertical-align: middle; margin-right: 5px;"> ${escapeHtml(mixtype)}`;
}

function getQueryParam(param) {
    const urlParams = new URLSearchParams(window.location.search);
    return urlParams.get(param);
}

function getRandomColor() {
    let hexColor;
    let r, g, b;
    let luminance;
    const minLuminance = 0.5; // Threshold for brightness (0.0 to 1.0)

    do {
        // Generate a random 6-digit hex color
        hexColor = Math.floor(Math.random() * 16777215).toString(16).padStart(6, '0');
        
        // Convert the hex color to its RGB components
        r = parseInt(hexColor.substring(0, 2), 16);
        g = parseInt(hexColor.substring(2, 4), 16);
        b = parseInt(hexColor.substring(4, 6), 16);

        // Calculate the relative luminance of the color
        // This is a more robust method than just checking each color channel
        luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255;

    } while (luminance < minLuminance);

    return hexColor;
}

function isCountedIngredient(step) {
    if (!step || !step.ingredient_name) return false;
    const ingredient = step.ingredient_name.toLowerCase();
    // A list of ingredients that are counted by item, not by weight.
    const mixtype = step.mixtype_name ? step.mixtype_name.toLowerCase() : '';
    if (ingredient === 'cherry' && mixtype === 'garnish') {
        return true;
    }
    const countedItems = [
        'mint sprig',
        'mint leaf',
        'orange slice',
        'cucumber slice',
        'tangerine zest',
        'lime wheel',
        'lemon peel','bitters','absinthe','fennel bulb','orange twist','cucumber ribbon',
        'orange peel','salt','kosher salt','ground black pepper','celery salt','goji berry','egg','cocoa powder','sugar','cayenne','whipped cream',
        'dill'
    ];
    return countedItems.some(item => ingredient.includes(item));
}

function renderCocktailHeader(cocktail) {
    const bgColor = getRandomColor();
    const intro = cocktail.cocktail_intro || '';
    const limit = 100;
    let introHTML;

    if (intro.length > limit) {
        const shortIntro = intro.substring(0, limit);
        introHTML = `
            <p class="text-black text-center">
                <span id="shortIntro">${shortIntro}...</span>
                <span id="fullIntro" style="display: none;">${intro}</span>
                <a href="#" id="readMoreLink" class="text-blue-700 hover:underline">Read more</a>
            </p>
        `;
    } else {
        introHTML = `<p class="text-black text-center">${intro}</p>`;
    }

    return `
        <div id="cocktailHeaderContainer" class="w-full h-48 rounded-t-lg flex flex-col items-center justify-center p-5 text-center"
             style="background-color: #${bgColor};">
            <h3 class="text-3xl font-extrabold text-white mb-2">${cocktail.name}</h3>
            ${introHTML}
        </div>
    `;
}

function setGlobalRecipeInfo(cocktail, steps) {
    const drinkTitleEl = document.getElementById('drinkTitle');
    // const ingredientsSummaryEl = document.getElementById('ingredientsSummary');
    const finalWeightEl = document.getElementById('finalWeight');

    if (drinkTitleEl) drinkTitleEl.textContent = `Making ${cocktail.name}`;
    // if (ingredientsSummaryEl) ingredientsSummaryEl.textContent = cocktail.ingredients || 'No ingredients listed';

    const totalWeight = steps.reduce((sum, step) => {
        if (isCountedIngredient(step)) {
            console.log("step,sum",step,sum);
            return sum;
        }
        return sum + (parseFloat(step.quantity_miligrams) || 0);
    }, 0);
    if (finalWeightEl) finalWeightEl.textContent = `Recipe Target: ${(totalWeight / 1000).toFixed(1)}g`;
}

function renderStepsList(steps, currentIndex) {
    const stepsListEl = document.getElementById('stepsList');
    if (!stepsListEl) return;

    const html = steps.map((step, index) => {
        let classes = 'p-4 rounded-lg shadow mb-2 transition-all duration-300 cursor-pointer';
        let content;
        const mixtypeDisplay = mixtypeToSvg(step.mixtype_name);

        if (isCountedIngredient(step) && step.quantity_value) {
            content = `<strong>Step ${index + 1}:</strong> ${mixtypeDisplay} - ${step.quantity_value} ${step.ingredient_name}`;
            console.log("renderStepsList1",content);
        } else if (step.quantity_miligrams && parseFloat(step.quantity_miligrams) > 0) {
            content = `<strong>Step ${index + 1}:</strong> ${mixtypeDisplay} - ${step.ingredient_name} - ${(parseFloat(step.quantity_miligrams) / 1000).toFixed(1)}g`;
            console.log("renderStepsList2",content);
        } else {
            content = `<strong>Step ${index + 1}:</strong> ${mixtypeDisplay} - ${step.ingredient_name || ''}`;
            console.log("renderStepsList 3 ",content);
        }

        if (index < currentIndex) {
            classes += ' bg-green-100 text-green-800 opacity-60 ';
        } else if (index === currentIndex) {
            classes += ' bg-blue-100 text-blue-900 scale-105 shadow-lg border-2 border-blue-400';
            content += `<div class="mt-2 text-base text-blue-700 font-semibold">${step.mixtype_description || ''}</div>`;
        } else {
            classes += ' bg-gray-100 text-gray-500';
        }
        return `<li class="${classes}">${content}</li>`;
    }).join('');

    stepsListEl.innerHTML = html;
}

function guideUserThroughSteps(steps) {
    let currentStepIndex = 0;
    let makingCocktail = false;
    let inPreviewMode = false;
    // New variables for tracking state
    let liveStepIndex = 0;
    let totalWeightInCup = 0; // New: track cumulative weight

    const startButton = document.getElementById('startCocktailButton');
    const previewButton = document.getElementById('previewStepsButton');
    const nextButton = document.getElementById('nextStepButton');
    const prevButton = document.getElementById('prevStepButton');
    const tareButton = document.getElementById('tareButton');
    const finalComparisonEl = document.getElementById('finalComparison');

    if (backToAllButton) {
        backToAllButton.classList.remove('hidden');
        backToAllButton.addEventListener('click', () => {
            // Navigate back to the main page
            window.location.href = 'index.adp';
        });
    }
    function showStep(index) {
        if (index < 0) return;

        // Hide final comparison unless it's the last step
        if (finalComparisonEl) finalComparisonEl.innerHTML = '';
        prevButton.style.display = index > 0 ? 'block' : 'none';
        nextButton.style.display = 'block';

        // Handle completion state
        if (index >= steps.length) {
            if (inPreviewMode) {
                const howto = steps.find(s => s.howto_name)?.howto_name;
                const endofpreview = ` ${howto}.`;
                ui.updateGuidance(endofpreview, 'success');
            } else {
                const howto = steps.find(s => s.howto_name)?.howto_name;
                let completionMessage = 'Cocktail complete!';
                if (howto) {
                    completionMessage += ` ${howto}.`;
                }
                completionMessage += ' Enjoy your drink.';
                ui.updateGuidance(completionMessage, 'success');
            }
            nextButton.style.display = 'none';

            if (!inPreviewMode) {
                scale.stopCocktailDosingStep();
            }
            renderStepsList(steps, index);

            const recipeTotalWeight = steps.reduce((sum, step) => {
                if (isCountedIngredient(step)) return sum;
                return sum + (parseFloat(step.quantity_miligrams) || 0);
            }, 0) / 1000;
            // The final weight is the sum of all the recorded step deltas.
            if (!inPreviewMode) {
                const userTotalWeight = Object.values(user_added_weight).reduce((sum, weight) => sum + (weight || 0), 0);
                console.log("Final user added weight object:", user_added_weight);

                const difference = userTotalWeight - recipeTotalWeight;

                if (finalComparisonEl) {
                    finalComparisonEl.innerHTML = `
                        <div class="p-4 bg-gray-100 text-black rounded-lg">
                            <p class="font-semibold">Recipe Target: <span class="font-normal">${recipeTotalWeight.toFixed(1)}g</span></p>
                            <p class="font-semibold">Your Final Weight: <span class="font-normal">${userTotalWeight.toFixed(1)}g</span></p>
                            <p class="font-bold text-lg mt-2">Difference: <span class="font-bold ${difference >= 0 ? 'text-green-700' : 'text-red-700'}">${difference > 0 ? '+' : ''}${difference.toFixed(1)}g</span></p>
                        </div>
                    `;
                }
            }
            return;
        }

        const step = steps[index];
        const isHistoryView = index < liveStepIndex;

        if (inPreviewMode) {
            let guidanceText = `(Preview) Step ${index + 1}: `;
            if (isCountedIngredient(step) && step.quantity_value) {
                guidanceText += `Add ${step.quantity_value} ${step.ingredient_name}`;
            } else if (step.quantity_miligrams && parseFloat(step.quantity_miligrams) > 0) {
                const ingredientWeight = parseFloat(step.quantity_miligrams) / 1000.0;
                guidanceText += `Add ${ingredientWeight.toFixed(1)}g of ${step.ingredient_name}`;
            } else {
                guidanceText += `${step.mixtype_name || step.ingredient_name || 'Perform action'}`;
            }
            ui.updateGuidance(guidanceText);
        } else

        if (isHistoryView) {
            nextButton.style.display = 'block';
            const stepWeight = user_added_weight[`step_${index + 1}`] || 0;
            console.log(`Viewing step ${index + 1}. Recorded weight was ${stepWeight.toFixed(1)}g.`);

            let guidanceText = `(Viewing) Step ${index + 1}: ${step.ingredient_name}`;
            guidanceText += ` - You added ${stepWeight.toFixed(1)}g`;
            ui.updateGuidance(guidanceText);
            scale.stopCocktailDosingStep();
        } else if (isCountedIngredient(step) && step.quantity_value) {
            ui.updateGuidance(`Step ${index + 1}: Add ${step.quantity_value} ${step.ingredient_name}`);
            scale.stopCocktailDosingStep();
        } else {
            const ingredientWeight = parseFloat(step.quantity_miligrams) / 1000.0;
            if (isNaN(ingredientWeight) || ingredientWeight <= 0) {
                ui.updateGuidance(`Step ${index + 1}: ${step.mixtype_name || step.ingredient_name || 'Perform action'}`);
                nextButton.style.display = 'block';
                scale.stopCocktailDosingStep();
            } else {
                const targetTotalWeight = totalWeightInCup + ingredientWeight;
                ui.updateGuidance(`Step ${index + 1}: Add ${ingredientWeight.toFixed(1)}g of ${step.ingredient_name} (Target: ${targetTotalWeight.toFixed(1)}g total)`);
                nextButton.style.display = 'block'; // Always show the next button
                const settings = {
                    targetWeight: targetTotalWeight,
                    lowThreshold: targetTotalWeight - 1.0,
                    highThreshold: targetTotalWeight + 1.0,
                };
                scale.startCocktailDosingStep(settings);
            }
        }

        const percent = Math.round(((index + 1) / steps.length) * 100);
        ui.updateProgressBar(percent);
        ui.updateProgressBarColor('default');
        document.getElementById('progressPercent').textContent = `${percent}%`;

        renderStepsList(steps, index);
    }
    function advanceToNextStep() {
        currentStepIndex++;
        showStep(currentStepIndex);
    }

    function backToPrevStep() {
        if (currentStepIndex > 0) {
            currentStepIndex--;
            showStep(currentStepIndex);
        }
    }

    // Monkey-patching the state machine to get an event when a step is done.
    const originalSetState = scale.stateMachine.setCurrentState.bind(scale.stateMachine);
    scale.stateMachine.setCurrentState = (newState) => {
        const oldState = scale.stateMachine.currentState;
        originalSetState(newState);

        // When the scale's state machine determines the weight is stable and in range,
        // it transitions from MEASURING to REMOVAL_PENDING. For cocktail making,
        // we don't remove the glass. This is our cue to tare the scale and move to the next step.
        if (makingCocktail && oldState === SCALE_CONSTANTS.FSM_STATES.MEASURING && newState === SCALE_CONSTANTS.FSM_STATES.REMOVAL_PENDING) {
            console.log('Ingredient added successfully. Advancing to next step.');
            ui.updateGuidance('Good! Ready for the next ingredient.', 'success');

            const currentTotalWeightOnScale = ui.getWeightDisplay() || 0;
            const stepWeight = currentTotalWeightOnScale - totalWeightInCup;
            user_added_weight[`step_${liveStepIndex + 1}`] = stepWeight;
            totalWeightInCup = currentTotalWeightOnScale; // Update cumulative weight

            console.log(`Weight for step ${liveStepIndex + 1}: ${stepWeight.toFixed(1)}g. Total in cup: ${totalWeightInCup.toFixed(1)}g`);
            console.log("Current weight log:", user_added_weight);

            // Wait a bit for the user to see the success message, then advance.
            setTimeout(() => {
                liveStepIndex++;
                advanceToNextStep();
            }, 1500); // 1.5 second delay
        }
    };

    startButton.addEventListener('click', () => {
        if (!scale.ws || scale.ws.readyState !== ReconnectingWebSocket.OPEN) {
            alert('Scale not connected. Please wait.');
            return;
        }
        makingCocktail = true;
        startButton.style.display = 'none';
        tareButton.style.display = 'inline-block';
        if (previewButton) previewButton.style.display = 'none';
        ui.updateGuidance('Place your glass on the scale and press Tare.', 'info');

        const onTare = () => {
            tareButton.removeEventListener('click', onTare);
            scale.tare();
            tareButton.style.display = 'none';
            
            // Initialize/reset state variables for a new cocktail
            for (const key in user_added_weight) {
                delete user_added_weight[key];
            }
            totalWeightInCup = 0;
            liveStepIndex = 0;
            currentStepIndex = 0;

            setTimeout(() => showStep(currentStepIndex), 500);
        };
        tareButton.addEventListener('click', onTare);
    });

    if (previewButton) {
        previewButton.addEventListener('click', () => {
            inPreviewMode = true;
            makingCocktail = false;
            startButton.style.display = 'none';
            previewButton.style.display = 'none';
            ui.updateGuidance('Previewing steps. Use Next/Prev to navigate.', 'info');
            showStep(0);
        });
    }

    nextButton.addEventListener('click', () => {
        if (inPreviewMode) {
            advanceToNextStep();
        } else if (makingCocktail) {
            const isLiveStep = currentStepIndex === liveStepIndex;

            if (isLiveStep && currentStepIndex < steps.length) {
                // When user manually advances, stop any ongoing dosing operation.
                scale.stopCocktailDosingStep();

                const currentTotalWeightOnScale = ui.getWeightDisplay() || 0;
                const stepWeight = currentTotalWeightOnScale - totalWeightInCup;

                const step = steps[currentStepIndex];
                // For manual steps (non-dosing), we calculate the added weight.
                if (isCountedIngredient(step)) {
                    user_added_weight[`step_${currentStepIndex + 1}`] = 0;
                    console.log(`Step ${currentStepIndex + 1} is a counted ingredient, logging 0g.`);
                } else {
                    user_added_weight[`step_${currentStepIndex + 1}`] = stepWeight;
                    console.log(`Manually recorded weight for step ${currentStepIndex + 1}: ${stepWeight.toFixed(1)}g`);
                }

                totalWeightInCup = currentTotalWeightOnScale; // Update cumulative weight
                console.log(`Total in cup after manual step: ${totalWeightInCup.toFixed(1)}g`);
                console.log("Current weight log:", user_added_weight);

                // NO TARE
                liveStepIndex++;
            }
            advanceToNextStep();
        }
    });

    prevButton.addEventListener('click', () => {
        if (inPreviewMode) {
            backToPrevStep();
        } else if (makingCocktail) {
            backToPrevStep();
        }
    });
}

document.addEventListener('DOMContentLoaded', async () => {
       
    const cocktailId = getQueryParam('id');
    if (!cocktailId) {
        document.getElementById('cocktailHeader').innerHTML = '<p class="text-red-600">No cocktail selected.</p>';
        return;
    }

    const sqlManager = new SQLManager();
    await sqlManager.initDatabase();

    // Query cocktail info (name, ingredients)
    const cocktailRes = sqlManager.db.exec(
        `SELECT d.name, GROUP_CONCAT(i.name, ', ') as ingredients,c.cocktail_intro
         FROM drink d
         LEFT JOIN step s ON d.drink_id = s.drink_id
         LEFT JOIN ingredient i ON s.ingredient_id = i.ingredient_id
         Left JOIN cocktail_info c on c.drink_id=d.drink_id
         WHERE d.drink_id = ? AND s.sortorder IS NOT NULL
         GROUP BY d.drink_id, d.name;`,
        [cocktailId]
    );
    console.log("cocktialID",cocktailId)
    const cocktail = cocktailRes.length && cocktailRes[0].values.length
        ? {
            name: cocktailRes[0].values[0][0],
            ingredients: cocktailRes[0].values[0][1] || 'No ingredients listed',
            cocktail_intro:cocktailRes[0].values[0][2],
        }
        : null;

    if (cocktail) {
        document.getElementById('cocktailHeader').innerHTML = renderCocktailHeader(cocktail);
        const readMoreLink = document.getElementById('readMoreLink');
        if (readMoreLink) {
            readMoreLink.addEventListener('click', (e) => {
                e.preventDefault();
                const shortIntro = document.getElementById('shortIntro');
                const fullIntro = document.getElementById('fullIntro');
                const headerContainer = document.getElementById('cocktailHeaderContainer');
                if (fullIntro.style.display === 'none') {
                    shortIntro.style.display = 'none';
                    fullIntro.style.display = 'inline';
                    readMoreLink.textContent = 'Read less';
                    headerContainer.classList.remove('h-48');
                } else {
                    shortIntro.style.display = 'inline';
                    fullIntro.style.display = 'none';
                    readMoreLink.textContent = 'Read more';
                    headerContainer.classList.add('h-48');
                }
            });
        }
    } else {
        document.getElementById('cocktailHeader').innerHTML = '<p class="text-red-600">Cocktail not found.</p>';
        return;
    }

    // Query steps
    const stepsRes = sqlManager.db.exec(
        `SELECT
            d.name AS drink_name,
            s.sortorder,
            mi.name AS mixtype_name,
            mi.description AS mixtype_description,
            h.name AS howto_name,
            i.name AS ingredient_name,
            q.name AS quantity_value,
            q.miligrams AS quantity_miligrams
        FROM
            drink d
        JOIN
            step s ON d.drink_id = s.drink_id
        LEFT JOIN
            mixtype mi ON s.mixtype_id = mi.mixtype_id
        LEFT JOIN
            howto h ON s.howto_id = h.howto_id
        LEFT JOIN
            ingredient i ON s.ingredient_id = i.ingredient_id
        LEFT JOIN
            quantity q ON s.quantity_id = q.quantity_id
        WHERE
            d.drink_id = ? AND s.sortorder IS NOT NULL
        ORDER BY
            s.sortorder ASC;`,
        [cocktailId]
    );

    if (!stepsRes.length || !stepsRes[0].values.length) {
        document.getElementById('stepsList').innerHTML = '<li class="text-gray-500">No steps found for this cocktail.</li>';
        setGlobalRecipeInfo(cocktail, []);
        return;
    }

    const columns = stepsRes[0].columns;
    const steps = stepsRes[0].values.map(row => {
        const step = {};
        columns.forEach((col, idx) => step[col] = row[idx]);
        return step;
    });

    setGlobalRecipeInfo(cocktail, steps);
    guideUserThroughSteps(steps);
});
