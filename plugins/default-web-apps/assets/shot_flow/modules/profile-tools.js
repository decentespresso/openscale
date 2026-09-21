const clamp = (value, minimum, maximum) => Math.min(maximum, Math.max(minimum, value));
export const SCALE_PROFILE_SCHEMA = 'openscale.shot-profile/v1';

export function normalizeScaleProfile(value) {
    const points = value?.profile;
    if (!Array.isArray(points) || points.length < 2 || points.length > 128) return null;
    const profile = points.map(point => [Number(point?.[0]), Number(point?.[1])]);
    const ratio = Number(value.ratio);
    const duration = Number(value.duration);
    if (!value.id || !value.name || ratio <= 0 || duration < 3 ||
        profile.some(point => !point.every(Number.isFinite))) return null;
    return {
        schema: SCALE_PROFILE_SCHEMA,
        id: String(value.id),
        name: String(value.name).slice(0, 80),
        ratio,
        duration,
        profile: profile.map(([time, flow]) => [clamp(time), Math.max(0, flow)]),
        ...(value.source ? { source: value.source } : {})
    };
}

function weightAt(points, elapsed) {
    if (!points.length) return 0;
    if (elapsed <= points[0].elapsed) return points[0].weight;

    for (let index = 1; index < points.length; index++) {
        const previous = points[index - 1];
        const current = points[index];
        if (elapsed <= current.elapsed) {
            const fraction = (elapsed - previous.elapsed) /
                (current.elapsed - previous.elapsed || 1);
            return previous.weight + (current.weight - previous.weight) * fraction;
        }
    }

    return points.at(-1).weight;
}

export function recommendGrind(actual, ghost, targetDuration) {
    if (actual.length < 3 || ghost.length < 3 || targetDuration <= 0) return null;

    const actualEnd = actual.at(-1).elapsed;
    const comparisonEnd = Math.min(actualEnd, targetDuration);
    if (comparisonEnd < Math.max(3, targetDuration * 0.15)) return null;

    const targetWeight = weightAt(ghost, comparisonEnd);
    if (targetWeight < 2) return null;
    const actualWeight = weightAt(actual, comparisonEnd);
    const deviation = (actualWeight - targetWeight) / targetWeight;
    const percentage = Math.round(Math.abs(deviation) * 100);
    if (deviation > 0.06) {
        return {
            direction: 'finer',
            title: 'Grind finer',
            detail: `Extraction ran ${percentage}% ahead of the target profile.`,
            deviation
        };
    }
    if (deviation < -0.06) {
        return {
            direction: 'coarser',
            title: 'Grind coarser',
            detail: `Extraction ran ${percentage}% behind the target profile.`,
            deviation
        };
    }
    return {
        direction: 'hold',
        title: 'Keep this grind',
        detail: `Extraction stayed within ${Math.max(1, percentage)}% of the target profile.`,
        deviation
    };
}

export function createShotPreset(name, dose, points, id) {
    const cleanName = name.trim().slice(0, 40);
    const duration = points.at(-1)?.elapsed || 0;
    const yieldWeight = points.at(-1)?.weight || 0;
    if (!cleanName || dose <= 0 || duration < 3 || yieldWeight < 2) return null;

    const maximumFlow = Math.max(0, ...points.map(point => point.flow));
    if (maximumFlow < 0.05) return null;
    const step = Math.max(1, Math.floor(points.length / 32));
    const profile = points
        .filter((point, index) => index % step === 0 && point.elapsed >= 0)
        .map(point => [
            clamp(point.elapsed / duration, 0, 1),
            clamp(point.flow / maximumFlow, 0, 1)
        ]);

    if (!profile.length || profile[0][0] > 0) profile.unshift([0, 0]);
    profile[0] = [0, 0];
    if (profile.at(-1)[0] < 1) profile.push([1, 0]);
    else profile[profile.length - 1] = [1, 0];

    return normalizeScaleProfile({
        schema: SCALE_PROFILE_SCHEMA,
        id,
        name: cleanName,
        ratio: yieldWeight / dose,
        duration,
        profile
    });
}

export function createShotRecord({
    name,
    dose,
    points,
    targetPreset,
    recommendation,
    id,
    createdAt = Date.now()
}) {
    const cleanName = name.trim().slice(0, 40);
    const cleanPoints = points
        .filter(point =>
            Number.isFinite(point.elapsed) &&
            Number.isFinite(point.weight) &&
            Number.isFinite(point.flow)
        )
        .map(point => ({
            elapsed: point.elapsed,
            weight: point.weight,
            flow: point.flow
        }));
    const finalPoint = cleanPoints.at(-1);
    if (!cleanName || !finalPoint || dose <= 0 || finalPoint.elapsed < 3 || finalPoint.weight < 2) {
        return null;
    }

    return {
        id,
        name: cleanName,
        createdAt,
        dose,
        duration: finalPoint.elapsed,
        yieldWeight: finalPoint.weight,
        points: cleanPoints,
        target: targetPreset ? {
            schema: targetPreset.schema || SCALE_PROFILE_SCHEMA,
            id: targetPreset.id,
            name: targetPreset.name,
            ratio: targetPreset.ratio,
            duration: targetPreset.duration,
            profile: targetPreset.profile.map(point => [...point]),
            ...(targetPreset.source ? { source: targetPreset.source } : {})
        } : null,
        recommendation: recommendation ? { ...recommendation } : null
    };
}

export function validSavedProfiles(value) {
    if (!Array.isArray(value)) return [];
    return value.map(normalizeScaleProfile).filter(Boolean);
}