export const SHOT_PRESETS = Object.freeze({
    espresso: Object.freeze({
        schema: 'openscale.shot-profile/v1',
        id: 'espresso',
        name: 'Espresso',
        ratio: 2,
        duration: 25,
        profile: Object.freeze([[0, 0], [0.21, 1], [0.28, 0.48], [0.92, 0.24], [1, 0]]),
        source: Object.freeze({
            label: 'Decent: Flow profile for straight espresso',
            url: 'https://github.com/decentespresso/de1app/blob/main/de1plus/profiles/Flow%20profile%20for%20straight%20espresso.tcl',
            note: 'Machine input-flow stages normalized to the selected dose and yield.'
        })
    }),
    turbo: Object.freeze({
        schema: 'openscale.shot-profile/v1',
        id: 'turbo',
        name: 'Turbo',
        ratio: 4,
        duration: 30,
        profile: Object.freeze([[0, 0], [0.13, 0.83], [0.25, 1], [0.85, 0.75], [1, 0]]),
        source: Object.freeze({
            label: 'Decent: TurboTurbo',
            url: 'https://github.com/decentespresso/de1app/blob/main/de1plus/profiles/TurboTurbo.tcl',
            note: 'Machine input-flow stages normalized to the selected dose and yield.'
        })
    })
});

const clamp = (value, minimum, maximum) => Math.min(maximum, Math.max(minimum, value));

function profileArea(profile, endFraction = 1) {
    const end = clamp(endFraction, 0, 1);
    let area = 0;

    for (let index = 1; index < profile.length; index++) {
        const [leftX, leftY] = profile[index - 1];
        const [rightX, rightY] = profile[index];
        if (end <= leftX) break;

        const segmentEnd = Math.min(end, rightX);
        const width = segmentEnd - leftX;
        const segmentWidth = rightX - leftX;
        const endY = leftY + (rightY - leftY) * (width / segmentWidth);
        area += width * (leftY + endY) / 2;
        if (end <= rightX) break;
    }

    return area;
}

function profileValue(profile, fraction) {
    const position = clamp(fraction, 0, 1);
    for (let index = 1; index < profile.length; index++) {
        const [leftX, leftY] = profile[index - 1];
        const [rightX, rightY] = profile[index];
        if (position <= rightX) {
            return leftY + (rightY - leftY) * ((position - leftX) / (rightX - leftX));
        }
    }
    return profile.at(-1)[1];
}

export function ghostAt(elapsedSeconds, dose, preset) {
    const targetYield = Math.max(0, dose) * preset.ratio;
    const elapsed = clamp(elapsedSeconds, 0, preset.duration);
    const fraction = elapsed / preset.duration;
    const totalArea = profileArea(preset.profile);
    const flowScale = targetYield / (preset.duration * totalArea);

    return {
        flow: profileValue(preset.profile, fraction) * flowScale,
        weight: profileArea(preset.profile, fraction) * preset.duration * flowScale
    };
}

export function createGhostSeries(dose, preset, intervalSeconds = 0.25) {
    const points = [];
    for (let elapsed = 0; elapsed < preset.duration; elapsed += intervalSeconds) {
        points.push({ elapsed, ...ghostAt(elapsed, dose, preset) });
    }
    points.push({ elapsed: preset.duration, ...ghostAt(preset.duration, dose, preset) });

    for (let index = 0; index < points.length - 1; index++) {
        const current = points[index];
        const next = points[index + 1];
        current.flow = (next.weight - current.weight) / (next.elapsed - current.elapsed);
    }
    points.at(-1).flow = 0;
    return points;
}

export class FlowEstimator {
    constructor(windowMs = 700, smoothing = 0.34) {
        this.windowMs = windowMs;
        this.smoothing = smoothing;
        this.samples = [];
        this.flow = 0;
    }

    reset() {
        this.samples = [];
        this.flow = 0;
    }

    update(weight, nowMs) {
        this.samples.push({ weight, nowMs });
        const oldestAllowed = nowMs - this.windowMs;
        while (this.samples.length > 2 && this.samples[0].nowMs < oldestAllowed) {
            this.samples.shift();
        }
        if (this.samples.length < 3) return this.flow;

        const origin = this.samples[0].nowMs;
        const values = this.samples.map(sample => ({
            x: (sample.nowMs - origin) / 1000,
            y: sample.weight
        }));
        const meanX = values.reduce((sum, sample) => sum + sample.x, 0) / values.length;
        const meanY = values.reduce((sum, sample) => sum + sample.y, 0) / values.length;
        const numerator = values.reduce(
            (sum, sample) => sum + (sample.x - meanX) * (sample.y - meanY),
            0
        );
        const denominator = values.reduce(
            (sum, sample) => sum + (sample.x - meanX) ** 2,
            0
        );
        const rawFlow = denominator ? clamp(numerator / denominator, 0, 12) : 0;
        this.flow += (rawFlow - this.flow) * this.smoothing;
        return this.flow;
    }
}

export class ShotEngine {
    constructor({ autoStartFlow = 0.35, autoStartMs = 300, autoStopFlow = 0.12, autoStopMs = 2200 } = {}) {
        this.autoStartFlow = autoStartFlow;
        this.autoStartMs = autoStartMs;
        this.autoStopFlow = autoStopFlow;
        this.autoStopMs = autoStopMs;
        this.estimator = new FlowEstimator();
        this.reset();
    }

    reset() {
        this.status = 'ready';
        this.startMs = null;
        this.startWeight = 0;
        this.startCandidate = null;
        this.stopCandidateMs = null;
        this.points = [];
        this.latest = { elapsed: 0, weight: 0, flow: 0 };
        this.estimator.reset();
    }

    start(weight, nowMs) {
        this.status = 'recording';
        this.startMs = nowMs;
        this.startWeight = weight;
        this.startCandidate = null;
        this.stopCandidateMs = null;
        this.points = [{ elapsed: 0, weight: 0, flow: 0 }];
        this.latest = this.points[0];
    }

    stop() {
        if (this.status === 'recording') this.status = 'complete';
    }

    ingest(weight, nowMs, automatic = true) {
        const flow = this.estimator.update(weight, nowMs);

        if (this.status === 'ready' && automatic) {
            if (flow >= this.autoStartFlow) {
                if (!this.startCandidate) this.startCandidate = { nowMs, weight };
                if (nowMs - this.startCandidate.nowMs >= this.autoStartMs) {
                    this.start(this.startCandidate.weight, this.startCandidate.nowMs);
                }
            } else {
                this.startCandidate = null;
            }
        }

        if (this.status !== 'recording') {
            this.latest = { ...this.latest, flow };
            return this.latest;
        }

        const elapsed = (nowMs - this.startMs) / 1000;
        const point = {
            elapsed,
            weight: Math.max(0, weight - this.startWeight),
            flow
        };
        this.points.push(point);
        this.latest = point;

        if (automatic && elapsed >= 5 && point.weight >= 2) {
            if (flow <= this.autoStopFlow) {
                if (this.stopCandidateMs === null) this.stopCandidateMs = nowMs;
                if (nowMs - this.stopCandidateMs >= this.autoStopMs) this.stop();
            } else {
                this.stopCandidateMs = null;
            }
        }

        return point;
    }
}