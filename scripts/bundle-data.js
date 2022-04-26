import { readFile, open } from "fs/promises";

let input, data, output;

for (let a = 0; a < process.argv.length; ++a) {
    const arg = process.argv[a];
    switch (arg) {
    case "--input":
        input = process.argv[++a];
        break;
    case "--data":
        data = process.argv[++a];
        break;
    case "--output":
        output = process.argv[++a];
        break;
    }
}

if (input === undefined) {
    console.error("No input");
    process.exit(1);
}

if (data === undefined) {
    console.error("No data");
    process.exit(1);
}

if (output === undefined) {
    console.error("No output");
    process.exit(1);
}

async function go() {
    let inputd, datad;
    try {
        inputd = await readFile(input, "utf8");
        datad = await readFile(data, "utf8");
    } catch (e) {
        console.error(e.message);
        process.exit(1);
    }

    // replace data
    const needle = "\"$DATA_GOES_HERE$\"";
    const nidx = inputd.indexOf(needle);
    if (nidx === -1) {
        console.error(`Unable to find needle in ${input}`);
        process.exit(1);
    }

    try {
        const outfd = await open(output, "w");
        await outfd.write(inputd.substring(0, nidx));
        await outfd.write(datad);
        await outfd.write(inputd.substring(nidx + needle.length));
        await outfd.close();
    } catch (e) {
        console.error(e.message);
        process.exit(1);
    }
}

(async function() {
    await go();
})().then(() => {
    console.log(`Wrote ${output}.`);
}).catch(e => {
    console.error(e.message);
});
