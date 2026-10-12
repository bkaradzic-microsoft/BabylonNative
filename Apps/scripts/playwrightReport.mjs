import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export function summarizeReport(report) {
    const cases = [];
    function visit(suite) {
        for (const spec of suite.specs ?? []) {
            for (const test of spec.tests ?? []) {
                const result = test.results?.at(-1);
                const status = !result || (result.status === "skipped" && test.expectedStatus !== "skipped") ? "notRun" : result.status;
                cases.push({
                    title: spec.title,
                    project: test.projectName,
                    status,
                    outcome: test.status,
                    durationMs: (test.results ?? []).reduce((sum, attempt) => sum + attempt.duration, 0),
                    errors: (result?.errors ?? []).map((error) => error.message ?? error.value ?? String(error)),
                });
            }
        }
        for (const child of suite.suites ?? []) {
            visit(child);
        }
    }
    for (const suite of report.suites ?? []) {
        visit(suite);
    }
    const statuses = {};
    for (const test of cases) {
        statuses[test.status] = (statuses[test.status] ?? 0) + 1;
    }
    const errors = (report.errors ?? []).map((error) => error.message ?? String(error));
    return {
        selected: cases.length,
        statuses,
        wallSeconds: (report.stats?.duration ?? 0) / 1000,
        complete: cases.length > 0 && !statuses.notRun && !statuses.interrupted && !errors.length,
        successful: cases.length > 0 && cases.every((test) => test.status === "passed" && test.outcome === "expected") && !errors.length,
        errors,
        cases,
    };
}

const sceneFields = [
    "playgroundId", "scriptToRun", "sceneFolder", "sceneFilename", "functionToCall",
    "replace", "replaceUrl", "specificRoot", "rootPath",
];

function normalizeScene(test, root) {
    const scene = Object.fromEntries(sceneFields.map((key) => [key, test[key] ?? ""]));
    if (scene.playgroundId) {
        scene.playgroundId = scene.playgroundId.replace(/^#/, "");
        if (!scene.playgroundId.includes("#")) {
            scene.playgroundId += "#0";
        }
    }
    return JSON.stringify({
        ...scene,
        sourceRoot: test.sceneFolder || test.scriptToRun ? root ?? "" : "",
        renderCount: test.renderCount || 1,
        useLargeWorldRendering: test.useLargeWorldRendering ?? false,
        useReverseDepthBuffer: String(test.useReverseDepthBuffer ?? "false"),
        useNonCompatibilityMode: String(test.useNonCompatibilityMode ?? "false"),
    });
}

export function compareCoverage(legacy, shared, summary) {
    const tags = new Set(["native", "native-d3d11", "native-win32", "native-win32-d3d11"]);
    const isExcluded = (test) => test.excludeFromAutomaticTesting || test.excludedEngines?.some((tag) => tags.has(tag.toLowerCase()));
    const expectedTitles = shared.tests.filter((test) => !isExcluded(test)).map((test) => test.title);
    const expectedSet = new Set(expectedTitles);
    const resultByTitle = new Map();
    for (const test of summary.cases.filter((test) => test.project === "native")) {
        const results = resultByTitle.get(test.title) ?? [];
        results.push(test);
        resultByTitle.set(test.title, results);
    }
    const missing = expectedTitles.filter((title) => !resultByTitle.has(title));
    const unexpected = [...resultByTitle.keys()].filter((title) => !expectedSet.has(title));
    const duplicates = [...resultByTitle].filter(([, results]) => results.length !== 1).map(([title]) => title);
    const sharedSelection = {
        expected: expectedTitles.length,
        missing,
        unexpected,
        duplicates,
        complete: expectedTitles.length > 0 && expectedTitles.length === expectedSet.size &&
            !missing.length && !unexpected.length && !duplicates.length &&
            summary.cases.every((test) => test.project === "native"),
    };
    const cases = legacy.tests.filter((test) =>
        !test.excludeFromAutomaticTesting && !test.onlyVisual && !test.excludedGraphicsApis?.includes("D3D11")
    ).map((test) => {
        const identity = normalizeScene(test, legacy.root);
        const exact = shared.tests.filter((candidate) => normalizeScene(candidate, shared.root) === identity);
        const sameTitle = exact.filter((candidate) => candidate.title === test.title);
        const matches = sameTitle.length ? sameTitle : exact;
        const row = { legacyTitle: test.title, sharedTitle: null, status: "missing", detail: "", changedFields: [] };
        if (matches.length !== 1) {
            row.status = matches.length ? "ambiguous" : "missing";
            const sameName = shared.tests.find((candidate) => candidate.title === test.title);
            if (sameName && !matches.length) {
                const oldScene = JSON.parse(identity);
                const newScene = JSON.parse(normalizeScene(sameName, shared.root));
                row.changedFields = Object.keys(oldScene).filter((key) => oldScene[key] !== newScene[key]);
                row.detail = "Title exists, but scene source or render settings differ.";
            } else {
                row.detail = matches.length ? "Multiple scenes have matching source and render settings." : "No matching scene and render settings.";
            }
            return row;
        }
        const match = matches[0];
        row.sharedTitle = match.title;
        if (isExcluded(match)) {
            row.status = "excluded";
            row.detail = "Shared catalog excludes a legacy runnable case.";
            return row;
        }
        const results = resultByTitle.get(match.title) ?? [];
        row.status = results.length === 1 ? results[0].status : results.length ? "ambiguous" : "notRun";
        return row;
    });
    return {
        legacyRunnable: cases.length,
        coveredAndPassing: cases.filter((test) => test.status === "passed").length,
        sharedSelection,
        readyForCutover: sharedSelection.complete && summary.complete && summary.successful && cases.length > 0 && cases.every((test) => test.status === "passed"),
        cases,
    };
}

if (process.argv[1] && fs.realpathSync(path.resolve(process.argv[1])) === fs.realpathSync(fileURLToPath(import.meta.url))) {
    const [reportFile, legacyFile, sharedFile, outputFile, mode = "shadow"] = process.argv.slice(2);
    if (!reportFile || !legacyFile || !sharedFile || !outputFile || !["shadow", "replace"].includes(mode)) {
        throw new Error("Usage: node playwrightReport.mjs report.json legacy-config.json shared-config.json output.json [shadow|replace]");
    }
    const read = (filename) => JSON.parse(fs.readFileSync(filename, "utf8").replace(/^\uFEFF/, ""));
    const summary = summarizeReport(read(reportFile));
    const coverage = compareCoverage(read(legacyFile), read(sharedFile), summary);
    fs.writeFileSync(outputFile, JSON.stringify({ summary, coverage }, null, 2) + "\n");
    console.log(JSON.stringify({
        selected: summary.selected,
        statuses: summary.statuses,
        wallSeconds: summary.wallSeconds,
        legacyRunnable: coverage.legacyRunnable,
        coveredAndPassing: coverage.coveredAndPassing,
        readyForCutover: coverage.readyForCutover,
    }, null, 2));
    if (!coverage.sharedSelection.complete || !summary.complete || !summary.successful || (mode === "replace" && !coverage.readyForCutover)) {
        process.exitCode = 1;
    }
}
