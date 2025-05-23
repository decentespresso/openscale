export class DataExport {
    static getFormattedDate() {
        const now = new Date();
        const year = now.getFullYear();
        const month = String(now.getMonth() + 1).padStart(2, '0');
        const day = String(now.getDate()).padStart(2, '0');
        return `${year}${month}${day}`;
    }

    static exportToCSV(weightData) {
        if (weightData.length === 0) {
            return { 
                content: '', 
                filename: `dosing-assistant-${this.getFormattedDate()}.csv`, 
                type: 'text/csv' 
            };
        }
        
        const headers = ['Reading', 'Timestamp', 'Weight (g)', 'Target (g)', 'Low Threshold (g)', 'High Threshold (g)', 'Status'];
        const rows = weightData.map(reading => [
            reading.readings,
            reading.timestamp,
            reading.weight,
            reading.target,
            reading.lowThreshold,
            reading.highThreshold,
            reading.status
        ]);
        
        const csvContent = [
            headers.join(','),
            ...rows.map(row => row.join(','))
        ].join('\n');
        
        return {
            content: csvContent,
            filename: `dosing-assistant-${this.getFormattedDate()}.csv`,
            type: 'text/csv'
        };
    }
    
    static exportToJSON(weightData) {
        if (weightData.length === 0) {
            return { 
                content: '', 
                filename: `dosing-assistant-${this.getFormattedDate()}.json`, 
                type: 'application/json' 
            };
        }
        
        const jsonOutput = {
            export_date: new Date().toISOString(),
            total_readings: weightData.length,
            readings: weightData.map(reading => ({
                reading_number: reading.readings,
                timestamp: reading.timestamp,
                weight_g: reading.weight,
                target_weight_g: reading.target,
                thresholds: {
                    low_g: reading.lowThreshold,
                    high_g: reading.highThreshold
                },
                status: reading.status
            }))
        };
        
        return {
            content: JSON.stringify(jsonOutput, null, 2),
            filename: `dosing-assistant-${this.getFormattedDate()}.json`,
            type: 'application/json'
        };
    }
    
    static downloadFile(content, filename, contentType) {
        const blob = new Blob([content], { type: contentType });
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        a.click();
        window.URL.revokeObjectURL(url);
    }
}
