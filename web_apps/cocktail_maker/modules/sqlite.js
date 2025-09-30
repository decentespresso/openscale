export class SQLManager {
    constructor() {
        this.db = null;
        this.statusDiv = document.getElementById('dbstatus');
        this.queryArea = document.getElementById('query-area');
        // this.executeButton = document.getElementById('execute-button');
        this.resultsArea = document.getElementById('results-area');
    }

    async initDatabase() {
        try {
            this.statusDiv.textContent = 'Loading SQL.js WebAssembly module...';
            this.statusDiv.className = 'info';

            const SQL = await window.initSqlJs({
                locateFile: file => file
            });

            this.statusDiv.textContent = 'Fetching your .db file...';
            const response = await fetch('june_16pdt.db');
            if (!response.ok) {
                throw new Error(`HTTP error! Status: ${response.status} - Make sure 'june_16pdt.db' exists and is served by a web server.`);
            }
            const buffer = await response.arrayBuffer();

            this.statusDiv.textContent = 'Loading database into SQL.js...';
            this.db = new SQL.Database(new Uint8Array(buffer));
            this.statusDiv.textContent = 'Database loaded successfully! Ready to query.';
            this.statusDiv.className = 'hidden';
            // this.executeButton.disabled = false;
        } catch (err) {
            this.statusDiv.innerHTML = `<span class="error">Error initializing database: ${err.message}</span>`;
            console.error("Database initialization error:", err);
        }
    }

    executeQuery() {
        if (!this.db) {
            this.resultsArea.innerHTML = '<span class="error">Database not loaded yet. Please wait.</span>';
            return;
        }

        const query = this.queryArea.value;
        this.resultsArea.textContent = 'Executing query...';
        this.resultsArea.classList.remove('error');

        try {
            const res = this.db.exec(query);
            if (res.length === 0 || (res.length === 1 && res[0].columns.length === 0 && res[0].values.length === 0)) {
                this.resultsArea.textContent = 'Query executed successfully, but returned no results.';
                return;
            }
            const { columns, values } = res[0];
            let tableHtml = '<table><thead><tr>';
            columns.forEach(col => {
                tableHtml += `<th>${col}</th>`;
            });
            tableHtml += '</tr></thead><tbody>';
            values.forEach(row => {
                tableHtml += '<tr>';
                row.forEach(cell => {
                    tableHtml += `<td>${cell !== null ? cell : 'NULL'}</td>`;
                });
                tableHtml += '</tr>';
            });
            tableHtml += '</tbody></table>';
            this.resultsArea.innerHTML = tableHtml;
        } catch (err) {
            this.resultsArea.innerHTML = `<span class="error">SQL Error: ${err.message}</span>`;
            console.error("SQL query error:", err);
        }
    }
    getAllCocktails() {
        if (!this.db) return [];
        // This query gets each drink and a comma-separated list of its ingredients
        const res = this.db.exec(`
            SELECT d.drink_id as id, d.name,
                GROUP_CONCAT(i.name, ', ') as ingredients
            FROM drink d
            LEFT JOIN step s ON d.drink_id = s.drink_id
            LEFT JOIN ingredient i ON s.ingredient_id = i.ingredient_id
            GROUP BY d.drink_id, d.name
            ORDER BY d.name;
        `);
        if (!res.length) return [];
        return res[0].values.map(row => ({
            id: row[0],
            name: row[1],
            ingredients: row[2] || ''
        }));
    }
    /**
     * Search cocktails by name (case-insensitive, partial match)
     */
    searchCocktails(term) {
        if (!this.db) return [];
        const res = this.db.exec(
            `
            SELECT d.drink_id as id, d.name,
                GROUP_CONCAT(i.name, ', ') as ingredients
            FROM drink d
            LEFT JOIN step s ON d.drink_id = s.drink_id
            LEFT JOIN ingredient i ON s.ingredient_id = i.ingredient_id
            WHERE LOWER(d.name) LIKE ?
            GROUP BY d.drink_id, d.name
            ORDER BY d.name;
            `,
            [`%${term.toLowerCase()}%`]
        );
        if (!res.length) return [];
        return res[0].values.map(row => ({
            id: row[0],
            name: row[1],
            ingredients: row[2] || ''
        }));
    }
}