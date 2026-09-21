const COLORS = Object.freeze({
    text: '#a9b3aa',
    grid: '#2a352e',
    flow: '#f5c95c',
    ghostFlow: '#ff765f',
    weight: '#54d18c',
    ghostWeight: '#77b9d6'
});

const niceCeiling = value => {
    if (value <= 4) return 4;
    if (value <= 6) return 6;
    if (value <= 8) return 8;
    return Math.ceil(value / 5) * 5;
};

export function calculateChartBounds({
    actual = [],
    ghost = [],
    duration,
    targetYield,
    actualComplete = false
}) {
    const margin = Math.max(1, duration * 0.04);
    const actualEnd = (actual.at(-1)?.elapsed || 0) + (actualComplete ? margin : 0);
    const targetEnd = duration + margin;
    const maximumElapsed = Math.max(targetEnd, actualEnd, 10);
    const maximumActualWeight = Math.max(0, ...actual.map(point => point.weight));
    const maximumFlow = Math.max(
        4,
        ...ghost.map(point => point.flow),
        ...actual.map(point => point.flow)
    );

    return {
        minimumSeconds: -margin,
        maximumSeconds: Math.ceil(maximumElapsed / 5) * 5,
        maximumFlow: niceCeiling(maximumFlow),
        maximumWeight: niceCeiling(Math.max(targetYield, maximumActualWeight) * 1.08),
        margin
    };
}

export class ShotChart {
    constructor(canvas) {
        this.canvas = canvas;
        this.context = canvas.getContext('2d');
        this.actual = [];
        this.ghost = [];
        this.duration = 25;
        this.targetYield = 36;
        this.actualComplete = false;
        this.framePending = false;
        this.observer = new ResizeObserver(() => this.schedule());
        this.observer.observe(canvas.parentElement);
    }

    update(actual, ghost, duration, targetYield, actualComplete = false) {
        this.actual = actual;
        this.ghost = ghost;
        this.duration = duration;
        this.targetYield = targetYield;
        this.actualComplete = actualComplete;
        this.schedule();
    }

    seriesWithMargins() {
        const { margin } = calculateChartBounds({
            actual: this.actual,
            ghost: this.ghost,
            duration: this.duration,
            targetYield: this.targetYield,
            actualComplete: this.actualComplete
        });
        const firstGhost = this.ghost[0];
        const lastGhost = this.ghost.at(-1);
        const ghost = firstGhost && lastGhost ? [
            { elapsed: -margin, weight: firstGhost.weight, flow: 0 },
            ...this.ghost,
            { elapsed: lastGhost.elapsed + margin, weight: lastGhost.weight, flow: 0 }
        ] : [];
        const actual = this.actual.map(point => ({ ...point }));
        if (actual.length) {
            const firstActual = actual[0];
            actual.unshift({ elapsed: -margin, weight: firstActual.weight, flow: 0 });
            if (this.actualComplete) {
                const lastActual = actual.at(-1);
                lastActual.flow = 0;
                actual.push({
                    elapsed: lastActual.elapsed + margin,
                    weight: lastActual.weight,
                    flow: 0
                });
            }
        }
        return { actual, ghost, margin };
    }

    schedule() {
        if (this.framePending) return;
        this.framePending = true;
        requestAnimationFrame(() => {
            this.framePending = false;
            this.draw();
        });
    }

    draw() {
        const { canvas, context } = this;
        const bounds = canvas.getBoundingClientRect();
        const ratio = Math.min(window.devicePixelRatio || 1, 2);
        const width = Math.max(1, Math.round(bounds.width * ratio));
        const height = Math.max(1, Math.round(bounds.height * ratio));
        if (canvas.width !== width || canvas.height !== height) {
            canvas.width = width;
            canvas.height = height;
        }

        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.clearRect(0, 0, bounds.width, bounds.height);

        const compact = bounds.width < 520;
        const padding = compact
            ? { top: 20, right: 42, bottom: 34, left: 38 }
            : { top: 24, right: 52, bottom: 36, left: 46 };
        const plot = {
            left: padding.left,
            top: padding.top,
            width: bounds.width - padding.left - padding.right,
            height: bounds.height - padding.top - padding.bottom
        };
        const { actual, ghost } = this.seriesWithMargins();
        const chartBounds = calculateChartBounds({
            actual: this.actual,
            ghost: this.ghost,
            duration: this.duration,
            targetYield: this.targetYield,
            actualComplete: this.actualComplete
        });
        const minSeconds = chartBounds.minimumSeconds;
        const maxSeconds = chartBounds.maximumSeconds;
        const maxFlow = chartBounds.maximumFlow;
        const maxWeight = chartBounds.maximumWeight;
        const x = elapsed => plot.left + ((elapsed - minSeconds) / (maxSeconds - minSeconds)) * plot.width;
        const flowY = flow => plot.top + plot.height - (flow / maxFlow) * plot.height;
        const weightY = weight => plot.top + plot.height - (weight / maxWeight) * plot.height;

        this.drawGrid(plot, maxSeconds, maxFlow, maxWeight, x, flowY, weightY, compact);
        this.drawSeries(ghost, x, flowY, 'flow', COLORS.ghostFlow, [7, 6], 2);
        this.drawSeries(ghost, x, weightY, 'weight', COLORS.ghostWeight, [3, 5], 1.5);
        this.drawSeries(actual, x, weightY, 'weight', COLORS.weight, [], 2);
        this.drawSeries(actual, x, flowY, 'flow', COLORS.flow, [], 2.5);
    }

    drawGrid(plot, maxSeconds, maxFlow, maxWeight, x, flowY, weightY, compact) {
        const { context } = this;
        context.save();
        context.font = `${compact ? 10 : 11}px Bahnschrift, sans-serif`;
        context.fillStyle = COLORS.text;
        context.strokeStyle = COLORS.grid;
        context.lineWidth = 1;

        const horizontalLines = 4;
        for (let index = 0; index <= horizontalLines; index++) {
            const fraction = index / horizontalLines;
            const y = plot.top + plot.height * fraction;
            context.beginPath();
            context.moveTo(plot.left, y);
            context.lineTo(plot.left + plot.width, y);
            context.stroke();

            const flowLabel = (maxFlow * (1 - fraction)).toFixed(0);
            const weightLabel = (maxWeight * (1 - fraction)).toFixed(0);
            context.textAlign = 'right';
            context.fillText(flowLabel, plot.left - 8, y + 4);
            context.textAlign = 'left';
            context.fillText(weightLabel, plot.left + plot.width + 8, y + 4);
        }

        const step = maxSeconds <= 30 ? 5 : 10;
        for (let elapsed = 0; elapsed <= maxSeconds; elapsed += step) {
            const lineX = x(elapsed);
            context.beginPath();
            context.moveTo(lineX, plot.top);
            context.lineTo(lineX, plot.top + plot.height);
            context.stroke();
            context.textAlign = 'center';
            context.fillText(`${elapsed}s`, lineX, plot.top + plot.height + 22);
        }

        context.fillStyle = COLORS.flow;
        context.textAlign = 'left';
        context.fillText('g/s', plot.left, plot.top - 8);
        context.fillStyle = COLORS.weight;
        context.textAlign = 'right';
        context.fillText('g', plot.left + plot.width, plot.top - 8);
        context.restore();
    }

    drawSeries(points, x, y, key, color, dash, lineWidth, stepped = false) {
        if (points.length < 2) return;
        const { context } = this;
        context.save();
        context.beginPath();
        context.strokeStyle = color;
        context.lineWidth = lineWidth;
        context.lineJoin = 'round';
        context.lineCap = 'round';
        context.setLineDash(dash);
        points.forEach((point, index) => {
            const pointX = x(point.elapsed);
            const pointY = y(point[key]);
            if (index === 0) context.moveTo(pointX, pointY);
            else {
                if (stepped) context.lineTo(pointX, y(points[index - 1][key]));
                context.lineTo(pointX, pointY);
            }
        });
        context.stroke();
        context.restore();
    }
}