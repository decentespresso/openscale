export class DataExport {
    static exportToCSV(weightData) {
        const headers = ['Timestamp', 'Weight (g)', 'Elapsed Time (s)'];
        const csvContent = [
            headers.join(','),
            ...weightData.map(reading => [
                reading.timestamp,
                reading.weight,
                reading.elapsedTime
            ].join(','))
        ].join('\n');

        return {
            content: csvContent,
            filename: `scale-readings-${new Date().toISOString().split('T')[0]}.csv`,
            type: 'text/csv'
        };
    }

    static exportToJSON(weightData) {
        const jsonOutput = {
            export_date: new Date().toISOString(),
            total_readings: weightData.length,
            readings: weightData
        };

        return {
            content: JSON.stringify(jsonOutput, null, 2),
            filename: `scale-readings-${new Date().toISOString().split('T')[0]}.json`,
            type: 'application/json'
        };
    }

    static downloadFile(content, filename, contentType) {
        const blob = new Blob([content], { type: contentType });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }
}
