import csv
import io
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
CHECK = r'''
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
(async () => {
  const results = [];
  for (const app of ['dosing_assistant', 'Weigh_Save']) {
    const source = fs.readFileSync(path.join(process.argv[1], app, 'modules/export.js'), 'utf8');
    const module = new vm.SourceTextModule(source);
    await module.link(() => { throw new Error('Unexpected import'); });
    await module.evaluate();
    const readings = ['28/09/2026, 14:00:00', 'quoted "date"\r\nnext line', 'plain'].map(timestamp => ({
      readings: 1, timestamp, weight: '10.0', elapsedTime: 2,
      target: 10, lowThreshold: 9, highThreshold: 11, status: 'OK'
    }));
    results.push(module.namespace.DataExport.exportToCSV(readings).content);
  }
  console.log(JSON.stringify(results));
})().catch(error => { console.error(error); process.exitCode = 1; });
'''


def main():
    result = subprocess.run(["node", "--experimental-vm-modules", "-e", CHECK,
                             str(ROOT / "plugins/default-web-apps/assets")],
                            check=True, capture_output=True, text=True)
    for content, columnCount in zip(json.loads(result.stdout), (7, 3)):
        rows = list(csv.reader(io.StringIO(content, newline=""), strict=True))
        assert len(rows) == 4
        assert all(len(row) == columnCount for row in rows)
        timestampColumn = 1 if columnCount == 7 else 0
        assert [row[timestampColumn] for row in rows[1:]] == [
            "28/09/2026, 14:00:00", 'quoted "date"\r\nnext line', "plain"]
        assert all(row[timestampColumn + 1] == "10.0" for row in rows[1:])
    print("Dosing and Weigh Save CSV round-trip commas, quotes and newlines")


if __name__ == "__main__":
    main()
