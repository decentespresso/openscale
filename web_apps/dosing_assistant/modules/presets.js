export class PresetManager {
    constructor() {
        this.scale = null;
    }
    
    init(scale) {
        this.scale = scale;
    }
    
    setupPresetHandlers() {
        console.log('setupPresetHandlers() is being called');
        const presetSelect = document.getElementById('presetSelect');
        
        if (presetSelect) {
            presetSelect.addEventListener('change', (e) => {
                const selectedValue = e.target.value;
                console.log('Preset selected:', selectedValue);
                
                if (selectedValue === 'save_preset') {
                    console.log('Save preset option selected');
                    this.saveCurrentPreset();
                    e.target.value = ''; // Reset dropdown after saving
                } else if (selectedValue) {
                    this.loadPreset(selectedValue);
                }
            });
        } else {
            console.error('Preset select element not found');
        }
    }
    
    saveCurrentPreset() {
        const objectName = document.getElementById('objectName').value.trim();
        console.log('Attempting to save preset for:', objectName);
        
        if (!objectName) {
            console.warn('Save preset failed: No object name provided');
            alert('Please enter an object name');
            return;
        }

        const targetWeightInput = document.getElementById('targetWeight');
        const highThresholdInput = document.getElementById('highThreshold');
        const lowThresholdInput = document.getElementById('lowThreshold');

        if (!targetWeightInput || !highThresholdInput || !lowThresholdInput) {
            console.error('One or more input elements not found');
            return;
        }

        const preset = {
            name: objectName,
            settings: {
                targetWeight: parseFloat(targetWeightInput.value),
                highThreshold: parseFloat(highThresholdInput.value),
                lowThreshold: parseFloat(lowThresholdInput.value),                
            }
        };

        console.log('Saving preset with settings:', preset);
        this.savePreset(preset);
        this.updatePresetList();
        console.log('Preset saved successfully:', preset);
        alert(`Object "${objectName}" saved successfully`);
    }
    
    savePreset(preset) {
        const presets = this.getPresets();
        presets[preset.name] = preset;
        localStorage.setItem('decentScalePresets', JSON.stringify(presets));
    }
    
    getPresets() {
        const presetsJson = localStorage.getItem('decentScalePresets');
        return presetsJson ? JSON.parse(presetsJson) : {};
    }
    
    loadPreset(name) {
        console.log('loadPreset function called with name:', name);
        const preset = this.getPreset(name);
        
        if (!preset || !preset.settings) {
            console.warn(`Preset "${name}" not found or has invalid settings.`);
            return;
        }
        
        document.getElementById('objectName').value = name;
        document.getElementById('targetWeight').value = preset.settings.targetWeight;
        document.getElementById('lowThreshold').value = preset.settings.lowThreshold;
        document.getElementById('highThreshold').value = preset.settings.highThreshold;
        localStorage.setItem('lastUsedPreset', name);
    }
    
    getPreset(name) {
        const presets = this.getPresets();
        return presets[name];
    }
    
    loadPresets() {
        const presets = this.getPresets();
        const presetSelect = document.getElementById('presetSelect');
        
        if (!presetSelect) {
            console.error('Preset select element not found');
            return;
        }
        
        // Clear existing options except first two (default and save)
        while (presetSelect.options.length > 2) {
            presetSelect.remove(2);
        }
        
        // Add presets to dropdown
        Object.keys(presets).forEach(name => {
            const option = document.createElement('option');
            option.value = name;
            option.textContent = name;
            presetSelect.appendChild(option);
        });
        
        // Load last used preset if available
        const lastUsed = localStorage.getItem('lastUsedPreset');
        if (lastUsed && presets[lastUsed]) {
            this.loadPreset(lastUsed);
            presetSelect.value = lastUsed;
        }
    }
    
    updatePresetList() {
        this.loadPresets();
    }
}
