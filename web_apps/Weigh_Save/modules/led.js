export class Led { // methods to switch on and off LED panels on Decent scale.
    constructor(scale) {
        this.ledOn = false;
        this.scale = scale;
        this.toggleButton = document.getElementById('toggleLed');
        this.updateButtonText();
    }

    async _led_on() {
        console.log('Turning LED on...');
        await this.scale._send([0x03, 0x0A, 0x01, 0x01, 0x00, 0x00, 0x09]);
        this.ledOn = true;
        this.updateButtonText();
        console.log('LED should be on, state:', this.ledOn);
    }

    async _led_off() {
        console.log('Turning LED off...');
        await this.scale._send([0x03, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x09]);
        this.ledOn = false;
        this.updateButtonText();
        console.log('LED should be off, state:', this.ledOn);
    }

    updateButtonText() {
        if (this.toggleButton) {
            if (this.ledOn){this.toggleButton.textContent ='LED OFF';}
            else{this.toggleButton.textContent ='LED ON';}
            
        }
    }

    async toggleLed() {
        try {
            console.log('Toggle LED requested, current state:', this.ledOn);
            if (this.ledOn) {
                await this.scale.executeCommand(() => this._led_off());
            } else {
                await this.scale.executeCommand(() => this._led_on());
            }
        } catch (error) {
            console.error('Toggle LED error:', error);
            this.scale.statusDisplay.textContent = `Status: LED Toggle Error - ${error.message}`;
        }
    }
}
