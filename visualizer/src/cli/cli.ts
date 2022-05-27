#!/usr/bin/env node

import { Model } from "../model/Model";
import { readFile } from "fs/promises";

const args = process.argv.slice(2);

let input = "mtrackp.data";

for (let i = 0; i < args.length; ++i) {
    if (args[i] === "--input" && i + 1 < args.length) {
        input = args[i + 1];
    }
}

console.log("loading", input);

(async function() {

    const data = await readFile(input);
    if (!(data instanceof Uint8Array)) {
        console.error("no data");
        process.exit(1);
    }
    const model = new Model(data.buffer);

    console.log("parsing data");
    model.parse();
    console.log("parsed, processing data");
    console.log(model.snapshots.length, "snapshots");
    console.log(model.memories.length, "memories");
    console.log((model.stackStrings.reduce((prev, cur) => prev + cur.length, 0) / (1024 * 1024)).toFixed(2), "MB strings");
    console.log((model.stacks.reduce((prev, cur) => prev + (cur.length * 8), 0)).toFixed(2), "MB stacks");

    const mb = 1024 * 1024;
    const peak = [0, 0, 0];
    for (const memory of model.memories) {
        if (memory.pageFault + memory.malloc > peak[0]) {
            peak[0] = memory.pageFault + memory.malloc;
            peak[1] = memory.pageFault;
            peak[2] = memory.malloc;
        }
    }

    console.log(`peaked at ${(peak[0] / mb).toFixed(2)}Mb, pf ${(peak[1] / mb).toFixed(2)}Mb, malloc ${(peak[2] / mb).toFixed(2)}Mb`);

})().then(() => {
    process.exit(0);
}).catch(e => {
    console.error(e.message);
    process.exit(1);
});
