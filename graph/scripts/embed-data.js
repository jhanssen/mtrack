const fs = require("fs");

let input, data, output;

const numArgs = process.argv.length;
for (let idx = 2; idx < numArgs; ++idx) {
    const arg = process.argv[idx];
    if (arg === "--input" && idx + 1 < numArgs) {
        input = process.argv[++idx];
    } else if (arg === "--data" && idx + 1 < numArgs) {
        data = process.argv[++idx];
    } else if (arg === "--output" && idx + 1 < numArgs) {
        output = process.argv[++idx];
    }
}

if (input === undefined) {
    console.error("no input");
    process.exit(1);
}

if (data === undefined) {
    console.error("no data");
    process.exit(1);
}

if (output === undefined) {
    console.error("no output");
    process.exit(1);
}

let inputfile, datafile;

try {
    inputfile = fs.readFileSync(input, "utf8");
    datafile = fs.readFileSync(data, "utf8");
} catch (e) {
    console.error(e.message);
    process.exit(1);
}

// find the data
const needle = "\"$DATA_GOES_HERE$\"";

const where = inputfile.indexOf(needle);
if (where === -1) {
    console.error(`could not file "${needle}" in ${input}`);
    process.exit(1);
}

let wfd;

try {
    wfd = fs.openSync(output, "w");

    fs.writeSync(wfd, inputfile.substring(0, where));
    fs.writeSync(wfd, datafile);
    fs.writeSync(wfd, inputfile.substring(where + needle.length));

    fs.closeSync(wfd);
} catch (e) {
    console.error(e.message);
    process.exit(1);
}

console.log(`wrote ${output}.`);
