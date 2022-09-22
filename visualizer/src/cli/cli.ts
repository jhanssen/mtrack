#!/usr/bin/env node

import { Model } from "../model/Model";
import { gunzip } from "zlib";
import { promisify } from "util";
import { readFile } from "fs/promises";

const gunzipPromise = promisify(gunzip);
const args = process.argv.slice(2);

let input = "mtrackp.data";

if (args.length > 0) {
    let hasInput = false;
    for (let i = 0; i < args.length; ++i) {
        if (args[i] === "--input" && i + 1 < args.length) {
            input = args[i + 1];
            hasInput = true;
        }
    }
    if (!hasInput) {
        console.error("no --input argument");
        process.exit(1);
    }
}

console.log("loading", input);

async function readData(): Uint8Array | undefined {
    if (input.endsWith(".html")) {
        const html = await readFile(input, "utf8");
        const dataOffset = html.indexOf("\"data:application/octet-binary;base64,");
        if (dataOffset === -1) {
            return undefined;
        }
        const dataEnd = html.indexOf("\"", dataOffset + 38);
        if (dataEnd === -1) {
            return undefined;
        }
        const b64data = Buffer.from(html.substring(dataOffset + 38, dataEnd), "base64");
        const rawdata = await gunzipPromise(b64data);
        return rawdata;
    }
    const data = await readFile(input);
    return data;
}

(async function() {
    const data = await readData();
    if (!(data instanceof Uint8Array)) {
        console.error("no data");
        process.exit(1);
    }
    const model = new Model(data.buffer);
    const mb = 1024 * 1024;

    console.log("parsing data");
    model.parse();
    console.log("parsed, processing data");
    console.log(model.snapshots.length, "snapshots");
    console.log(model.memories.length, "memories");
    console.log((model.stackStrings.reduce((prev, cur) => prev + cur.length, 0) / mb).toFixed(2), "MB strings");
    console.log((model.stacks.reduce((prev, cur) => prev + (cur.length * 8), 0) / mb).toFixed(2), "MB stacks");

    const peak = [0, 0, 0];
    for (const memory of model.memories) {
        if (memory.pageFault + memory.malloc > peak[0]) {
            peak[0] = memory.pageFault + memory.malloc;
            peak[1] = memory.pageFault;
            peak[2] = memory.malloc;
        }
    }

    console.log(`peaked at ${(peak[0] / mb).toFixed(2)}MB, pf ${(peak[1] / mb).toFixed(2)}MB, malloc ${(peak[2] / mb).toFixed(2)}MB`);

})().then(() => {
    process.exit(0);
}).catch(e => {
    console.error(e.message);
    process.exit(1);
});
