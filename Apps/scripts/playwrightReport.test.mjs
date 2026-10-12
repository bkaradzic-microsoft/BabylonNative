import { test } from "node:test";
import assert from "node:assert/strict";
import { summarizeReport, compareCoverage } from "./playwrightReport.mjs";

const scene = { title: "Scene", playgroundId: "#ABC#1", renderCount: 2 };
function report(status = "passed", expectedStatus = "passed") {
    return {
        stats: { duration: 1000 },
        suites: [{ specs: [{ title: "Scene", tests: [{
            projectName: "native", expectedStatus, status: status === "passed" ? "expected" : "unexpected",
            results: [{ status, duration: 100 }],
        }] }] }],
    };
}

test("counts real passes and duration", () => {
    const summary = summarizeReport(report());
    assert.deepEqual(summary.statuses, { passed: 1 });
    assert.equal(summary.wallSeconds, 1);
    assert.equal(summary.successful, true);
});
test("never treats unexecuted tests as passing", () => {
    for (const results of [[], [{ status: "skipped", duration: 0 }]]) {
        const data = report();
        data.suites[0].specs[0].tests[0].results = results;
        const summary = summarizeReport(data);
        assert.deepEqual(summary.statuses, { notRun: 1 });
        assert.equal(summary.complete, false);
        assert.equal(summary.successful, false);
    }
});
test("empty, interrupted and global-error reports cannot succeed", () => {
    assert.equal(summarizeReport({}).successful, false);
    assert.equal(summarizeReport(report("interrupted")).complete, false);
    assert.equal(summarizeReport({ ...report(), errors: [{ message: "worker crash" }] }).successful, false);
});
test("only successful coverage permits cutover", () => {
    const summary = summarizeReport(report());
    const coverage = compareCoverage({ tests: [scene] }, { tests: [scene] }, summary);
    assert.equal(coverage.readyForCutover, true);
    assert.equal(compareCoverage({ tests: [scene] }, { tests: [scene] }, summarizeReport(report("failed"))).readyForCutover, false);
});
test("changed source, changed render count, exclusions and missing cases block cutover", () => {
    const summary = summarizeReport(report());
    for (const candidate of [
        { ...scene, playgroundId: "#ABC#2" },
        { ...scene, renderCount: 3 },
        { ...scene, replace: "old,new" },
        { ...scene, excludedEngines: ["native-d3d11"] },
        { ...scene, excludeFromAutomaticTesting: true },
    ]) {
        assert.equal(compareCoverage({ tests: [scene] }, { tests: [candidate] }, summary).readyForCutover, false);
    }
    assert.equal(compareCoverage({ tests: [scene] }, { tests: [] }, summary).readyForCutover, false);
});
test("legacy quarantines are not silently counted as coverage", () => {
    const legacy = { tests: [scene, { ...scene, title: "Excluded", excludedGraphicsApis: ["D3D11"] }] };
    const coverage = compareCoverage(legacy, { tests: [scene] }, summarizeReport(report()));
    assert.equal(coverage.legacyRunnable, 1);
});
test("ambiguous identities and missing execution block cutover", () => {
    const shared = { tests: [{ ...scene, title: "First" }, { ...scene, title: "Second" }] };
    assert.equal(compareCoverage({ tests: [scene] }, shared, summarizeReport(report())).cases[0].status, "ambiguous");
    assert.equal(compareCoverage({ tests: [scene] }, { tests: [scene] }, summarizeReport({})).cases[0].status, "notRun");
});
test("omitting a new shared test blocks cutover even when all legacy tests pass", () => {
    const shared = { tests: [scene, { title: "New coverage", playgroundId: "#NEW#1" }] };
    const coverage = compareCoverage({ tests: [scene] }, shared, summarizeReport(report()));
    assert.equal(coverage.coveredAndPassing, 1);
    assert.deepEqual(coverage.sharedSelection.missing, ["New coverage"]);
    assert.equal(coverage.readyForCutover, false);
});
test("duplicate results and other projects cannot establish full native coverage", () => {
    const summary = summarizeReport(report());
    summary.cases.push({ ...summary.cases[0] });
    assert.equal(compareCoverage({ tests: [scene] }, { tests: [scene] }, summary).readyForCutover, false);
    summary.cases.pop();
    summary.cases[0].project = "webgl2";
    assert.equal(compareCoverage({ tests: [scene] }, { tests: [scene] }, summary).readyForCutover, false);
});
test("coverage includes catalog roots for scripts and names changed fields", () => {
    const script = { title: "Scene", scriptToRun: "/scene.js", functionToCall: "createScene" };
    const coverage = compareCoverage(
        { root: "https://old.example", tests: [script] },
        { root: "https://new.example", tests: [script] },
        summarizeReport(report())
    );
    assert.equal(coverage.readyForCutover, false);
    assert.deepEqual(coverage.cases[0].changedFields, ["sourceRoot"]);
    const changed = compareCoverage({ tests: [scene] }, { tests: [{ ...scene, renderCount: 10 }] }, summarizeReport(report()));
    assert.deepEqual(changed.cases[0].changedFields, ["renderCount"]);
});
