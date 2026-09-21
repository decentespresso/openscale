import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import {
    de1PlusToScaleProfile,
    scaleProfileToDe1Plus,
    SCALE_PROFILE_SCHEMA
} from './shot_profile_converter.mjs';

const source = await readFile(new URL(
    '../plugins/default-web-apps/assets/shot_flow/modules/shot-model.js',
    import.meta.url
), 'utf8');
const {
    createGhostSeries,
    ghostAt,
    SHOT_PRESETS,
    ShotEngine
} = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

const chartSource = await readFile(new URL(
    '../plugins/default-web-apps/assets/shot_flow/modules/shot-chart.js',
    import.meta.url
), 'utf8');
const { calculateChartBounds } = await import(
    `data:text/javascript;base64,${Buffer.from(chartSource).toString('base64')}`
);

const profileToolsSource = await readFile(new URL(
    '../plugins/default-web-apps/assets/shot_flow/modules/profile-tools.js',
    import.meta.url
), 'utf8');
const { createShotPreset, createShotRecord, recommendGrind, validSavedProfiles } = await import(
    `data:text/javascript;base64,${Buffer.from(profileToolsSource).toString('base64')}`
);

test('ghost profiles finish at the dose-derived yield', () => {
    for (const preset of Object.values(SHOT_PRESETS)) {
        const dose = 18;
        const finish = ghostAt(preset.duration, dose, preset);
        const series = createGhostSeries(dose, preset);
        assert.ok(Math.abs(finish.weight - dose * preset.ratio) < 0.0001);
        assert.equal(finish.flow, 0);
        assert.equal(series.at(-1).elapsed, preset.duration);
        assert.match(preset.source.url, /^https:\/\/github\.com\/decentespresso\/de1app\//);
        assert.ok(preset.source.note.includes('normalized'));
        for (let index = 0; index < series.length - 1; index++) {
            const current = series[index];
            const next = series[index + 1];
            const yieldDifferential = (next.weight - current.weight) /
                (next.elapsed - current.elapsed);
            assert.ok(Math.abs(current.flow - yieldDifferential) < 0.000001);
        }
    }
});

test('sustained flow starts, records, and settles an automatic shot', () => {
    const engine = new ShotEngine();
    let weight = 0;

    for (let nowMs = 0; nowMs <= 8000; nowMs += 100) {
        weight += 0.2;
        engine.ingest(weight, nowMs);
    }

    assert.equal(engine.status, 'recording');
    assert.ok(engine.latest.weight > 10);
    assert.ok(engine.latest.flow > 1.8 && engine.latest.flow < 2.2);

    for (let nowMs = 8100; nowMs <= 12000; nowMs += 100) {
        engine.ingest(weight, nowMs);
    }

    assert.equal(engine.status, 'complete');
    assert.ok(engine.latest.elapsed > 8);
});

test('brief movement does not trigger automatic recording', () => {
    const engine = new ShotEngine();
    engine.ingest(0, 0);
    engine.ingest(0.2, 100);
    engine.ingest(0.4, 200);
    engine.ingest(0.4, 300);
    engine.ingest(0.4, 400);
    engine.ingest(0.4, 500);
    assert.equal(engine.status, 'ready');
});

test('chart bounds expand when actual yield exceeds the target', () => {
    const bounds = calculateChartBounds({
        actual: [{ elapsed: 25, weight: 58, flow: 8.7 }],
        ghost: createGhostSeries(18, SHOT_PRESETS.espresso),
        duration: SHOT_PRESETS.espresso.duration,
        targetYield: 36
    });

    assert.ok(bounds.maximumWeight > 58);
    assert.ok(bounds.maximumFlow > 8.7);
});

test('chart bounds expand when extraction outlasts the target', () => {
    const bounds = calculateChartBounds({
        actual: [{ elapsed: 37, weight: 48, flow: 0 }],
        ghost: createGhostSeries(18, SHOT_PRESETS.espresso),
        duration: SHOT_PRESETS.espresso.duration,
        targetYield: 36,
        actualComplete: true
    });

    assert.ok(bounds.maximumSeconds >= 38);
    assert.equal(bounds.maximumSeconds % 5, 0);
});

test('grind recommendation follows extraction pace against target', () => {
    const ghost = createGhostSeries(18, SHOT_PRESETS.espresso, 1);
    const faster = ghost.map(point => ({ ...point, weight: point.weight * 1.2 }));
    const slower = ghost.map(point => ({ ...point, weight: point.weight * 0.8 }));
    assert.equal(recommendGrind(faster, ghost, 25).direction, 'finer');
    assert.equal(recommendGrind(slower, ghost, 25).direction, 'coarser');
    assert.equal(recommendGrind(ghost, ghost, 25).direction, 'hold');
});

test('completed shot becomes a valid reusable profile', () => {
    const points = createGhostSeries(18, SHOT_PRESETS.espresso, 1);
    const preset = createShotPreset('House espresso', 18, points, 'custom-test');
    assert.equal(preset.name, 'House espresso');
    assert.ok(Math.abs(preset.ratio - 2) < 0.0001);
    assert.equal(preset.profile[0][0], 0);
    assert.equal(preset.profile.at(-1)[0], 1);
    assert.equal(validSavedProfiles([preset]).length, 1);
    assert.equal(validSavedProfiles([{ id: 'broken' }]).length, 0);
});

test('saved shot contains actual measurements instead of target points', () => {
    const target = SHOT_PRESETS.espresso;
    const actual = [
        { elapsed: 0, weight: 0, flow: 0 },
        { elapsed: 3, weight: 4, flow: 1.3 },
        { elapsed: 8, weight: 17, flow: 2.6 }
    ];
    const shot = createShotRecord({
        name: 'Morning shot',
        dose: 18,
        points: actual,
        targetPreset: target,
        recommendation: null,
        id: 'shot-test',
        createdAt: 1
    });

    assert.deepEqual(shot.points, actual);
    assert.notDeepEqual(shot.points, createGhostSeries(18, target));
    assert.equal(shot.target.id, target.id);
    assert.equal(shot.yieldWeight, 17);
});

test('DE1 flow profile converts to the canonical scale format', () => {
    const de1 = `flow_profile_preinfusion 4.2
flow_profile_preinfusion_time 6
flow_profile_hold 2
flow_profile_hold_time 8
flow_profile_decline 1
flow_profile_decline_time 17
final_desired_shot_weight_advanced 36
profile_title {Test DE1}`;
    const profile = de1PlusToScaleProfile(de1, { dose: 18 });
    assert.equal(profile.schema, SCALE_PROFILE_SCHEMA);
    assert.equal(profile.name, 'Test DE1');
    assert.equal(profile.ratio, 2);
    assert.equal(profile.duration, 31);
    assert.equal(profile.profile.at(-1)[1], 0);
});

test('DE1 advanced pressure stages use their flow limiters', () => {
    const de1 = `advanced_shot {{name fill pump flow flow 5 seconds 4} {name hold pump pressure max_flow_or_pressure 4 seconds 6} {name decline pump pressure max_flow_or_pressure 2 seconds 10}}
final_desired_shot_weight_advanced 36
profile_title {Advanced test}`;
    const profile = de1PlusToScaleProfile(de1, { dose: 18 });
    assert.equal(profile.duration, 20);
    assert.deepEqual(profile.profile, [[0, 0], [0.2, 1], [0.5, 0.8], [1, 0]]);
});

test('scale profile exports to DE1 and imports with core targets intact', () => {
    const exported = scaleProfileToDe1Plus(SHOT_PRESETS.espresso, {
        dose: 18,
        maximumFlow: 4
    });
    assert.match(exported, /settings_profile_type settings_2c/);
    assert.match(exported, /final_desired_shot_weight_advanced 36\.0/);
    const imported = de1PlusToScaleProfile(exported, { dose: 18 });
    assert.equal(imported.ratio, 2);
    assert.equal(imported.duration, SHOT_PRESETS.espresso.duration);
    assert.deepEqual(imported.profile, SHOT_PRESETS.espresso.profile);
});