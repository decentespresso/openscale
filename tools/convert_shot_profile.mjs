import { readFile, writeFile } from 'node:fs/promises';
import { de1PlusToScaleProfile, scaleProfileToDe1Plus } from './shot_profile_converter.mjs';

const [direction, inputPath, outputPath, ...argumentsList] = process.argv.slice(2);
if (!['to-scale', 'to-de1'].includes(direction) || !inputPath || !outputPath) {
    console.error('Usage: node tools/convert_shot_profile.mjs <to-scale|to-de1> <input> <output> [--dose N] [--ratio N] [--duration N] [--maximum-flow N]');
    process.exit(2);
}

const options = {};
for (let index = 0; index < argumentsList.length; index += 2) {
    const key = argumentsList[index]?.replace(/^--/, '').replace(/-([a-z])/g, (_, letter) => letter.toUpperCase());
    options[key] = Number(argumentsList[index + 1]);
}

const input = await readFile(inputPath, 'utf8');
const output = direction === 'to-scale'
    ? JSON.stringify(de1PlusToScaleProfile(input, options), null, 2) + '\n'
    : scaleProfileToDe1Plus(JSON.parse(input), options);
await writeFile(outputPath, output, 'utf8');