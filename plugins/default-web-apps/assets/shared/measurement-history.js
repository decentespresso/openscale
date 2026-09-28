(function (global) {
    global.MeasurementHistory = class {
        constructor(app, validate) {
            this.prefix = `hds.${app}.history.v1.`;
            this.validate = validate;
            this.lastWarning = null;
        }

        warn(message, error) {
            console.error(message, error);
            if (this.lastWarning !== message) global.alert?.(message);
            this.lastWarning = message;
        }

        load() {
            try {
                const keys = Array.from({length: localStorage.length}, (_, index) => localStorage.key(index))
                    .filter(key => key?.startsWith(this.prefix)).sort();
                return keys.flatMap(key => {
                    try {
                        const reading = JSON.parse(localStorage.getItem(key));
                        if (!this.validate(reading)) throw new Error('Invalid measurement');
                        return [reading];
                    } catch (error) {
                        this.warn('Some saved history could not be read. Existing data was left unchanged.', error);
                        return [];
                    }
                });
            } catch (error) {
                this.warn('History storage is unavailable. Export readings before closing this tab.', error);
                return [];
            }
        }

        append(reading) {
            try {
                if (!this.validate(reading)) throw new Error('Invalid measurement');
                const random = global.crypto.getRandomValues(new Uint32Array(4));
                const id = Array.from(random, value => value.toString(16).padStart(8, '0')).join('');
                const key = `${this.prefix}${String(Date.now()).padStart(13, '0')}.${id}`;
                localStorage.setItem(key, JSON.stringify(reading));
                return true;
            } catch (error) {
                this.warn('History could not be saved. Export readings before closing this tab.', error);
                return false;
            }
        }
    };
})(globalThis);
