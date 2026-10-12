import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const script = fileURLToPath(new URL("./runPlaywright.ps1", import.meta.url));
const scene = { title: "Scene", playgroundId: "#ABC#1" };

function fixture({ mode = "shadow", status = "passed", omitReport = false, extraShared = false, dotenv = false, environment = {} } = {}) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "bn-playwright-entry-"));
    function write(relative, content) {
        const target = path.join(root, relative);
        fs.mkdirSync(path.dirname(target), { recursive: true });
        fs.writeFileSync(target, content);
    }
    try {
        write("bin/git.cmd", "@echo off\r\nif \"%1\"==\"rev-parse\" echo 1111111111111111111111111111111111111111\r\nexit /b 0\r\n");
        write("host/Playground.exe", "Not executed: entry-point integration fixture");
        write("host/Scripts/config.json", JSON.stringify({ tests: [scene] }));
        write("suite/packages/tools/tests/test/playwright/nativePage.ts", "");
        write("suite/packages/tools/tests/test/visualization/config.json", JSON.stringify({
            tests: extraShared ? [scene, { title: "Additional case", playgroundId: "#NEW#1" }] : [scene],
        }));
        write("suite/packages/tools/tests/test/visualization/ReferenceImages/scene.png", "");
        if (dotenv) {
            write("suite/.env", "SCREENSHOT_THRESHOLD=1\n");
        }
        const report = {
            stats: { duration: 100 },
            suites: [{ specs: [{ title: "Scene", tests: [{
                projectName: "native", expectedStatus: "passed", status: status === "passed" ? "expected" : "unexpected",
                results: [{ status, duration: 10 }],
            }] }] }],
        };
        write("suite/node_modules/playwright/cli.js", `
const fs = require("fs");
const args = process.argv.slice(2);
console.log("fixture stdout");
console.error("fixture stderr");
if (!args.includes("--update-snapshots=none") || args[args.indexOf("--retries") + 1] !== "0") process.exit(9);
if (process.env.BROWSER === "BrowserStack") process.exit(9);
if (${!omitReport}) fs.writeFileSync(process.env.PLAYWRIGHT_JSON_OUTPUT_NAME, ${JSON.stringify(JSON.stringify(report))});
process.exit(${status === "passed" ? 0 : 1});
`);
        const env = { ...process.env };
        for (const name of Object.keys(env)) {
            if (name.toUpperCase() === "PATH") {
                env[name] = path.join(root, "bin") + path.delimiter + env[name];
            }
        }
        for (const name of ["CONFIG_PATH", "TEST_FILENAME", "AFFECTED_TAGS", "EXCLUDE_REGEX_ARRAY", "SCREENSHOT_THRESHOLD", "SCREENSHOT_MAX_PIXEL", "TIMEOUT"]) {
            delete env[name];
        }
        const output = path.join(root, "artifacts");
        const result = spawnSync("pwsh", [
            "-NoProfile", "-NonInteractive", "-File", script,
            "-BabylonJsRoot", path.join(root, "suite"),
            "-Playground", path.join(root, "host", "Playground.exe"),
            "-Artifacts", output,
            "-Mode", mode,
        ], { encoding: "utf8", env: { ...env, ...environment }, timeout: 20000 });
        if (result.error) {
            throw result.error;
        }
        const migrationPath = path.join(output, "migration.json");
        const log = (name) => fs.existsSync(path.join(output, name)) ? fs.readFileSync(path.join(output, name), "utf8") : "";
        return {
            status: result.status,
            text: result.stdout + result.stderr,
            migration: fs.existsSync(migrationPath) ? JSON.parse(fs.readFileSync(migrationPath, "utf8")) : undefined,
            runnerStdout: log("playwright.log"),
            runnerStderr: log("playwright-stderr.log"),
        };
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
}

test("CI entry point succeeds for a complete passing replacement", { skip: process.platform !== "win32" }, () => {
    const result = fixture({ mode: "replace" });
    assert.equal(result.status, 0, result.text);
    assert.equal(result.migration.coverage.readyForCutover, true);
    assert.match(result.runnerStdout, /fixture stdout/);
    assert.match(result.runnerStderr, /fixture stderr/);
});
test("shadow failures return nonzero and preserve the report", { skip: process.platform !== "win32" }, () => {
    const result = fixture({ status: "failed" });
    assert.notEqual(result.status, 0, result.text);
    assert.equal(result.migration.summary.statuses.failed, 1);
});
test("missing JSON is a failure even when the runner exits zero", { skip: process.platform !== "win32" }, () => {
    const result = fixture({ omitReport: true });
    assert.notEqual(result.status, 0, result.text);
    assert.match(result.text, /without a JSON report/);
});
test("an omitted shared case blocks replacement", { skip: process.platform !== "win32" }, () => {
    const result = fixture({ mode: "replace", extraShared: true });
    assert.notEqual(result.status, 0, result.text);
    assert.equal(result.migration.coverage.readyForCutover, false);
});
test("filtering overrides cannot silently narrow migration coverage", { skip: process.platform !== "win32" }, () => {
    const result = fixture({ environment: { AFFECTED_TAGS: "NONE" } });
    assert.notEqual(result.status, 0, result.text);
    assert.match(result.text, /AFFECTED_TAGS must be unset/);
});
test("dotenv cannot reintroduce tolerance overrides after preflight", { skip: process.platform !== "win32" }, () => {
    const result = fixture({ dotenv: true });
    assert.notEqual(result.status, 0, result.text);
    assert.match(result.text, /without a .env file/);
});
test("native migration does not start BrowserStack setup", { skip: process.platform !== "win32" }, () => {
    const result = fixture({ environment: { BROWSER: "BrowserStack" } });
    assert.equal(result.status, 0, result.text);
});
