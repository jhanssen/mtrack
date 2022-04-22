#!/usr/bin/env node

const fs = require("fs");
const Model = require("Model");

function usage(out)
{
    out(`${process.argv[0]} ${process.argv[1]} (--help|-h) (--until-ms <ms>) (--until-event <event>) <mtrack.json>`);
}

let dashDash = false;
let file;
let data;
let until = {};

for (let idx=2; idx<process.argv.length; ++idx) {
    const arg = process.argv[idx];
    if (arg === "--help" || arg === "-h") {
        usage(console.log.bind(console));
        process.exit(0);
    } else if (arg === "--") {
        dashDash = true;
    } else if (arg === "--until-ms" || arg.startsWith("--until-ms=")) {
        if (arg.length === 10) {
            until.ms = parseInt(process.argv[++idx], 10);
        } else {
            until.ms = parseInt(arg.substring(11), 10);
        }
        if (until.ms <= 0 || isNaN(until.ms)) {
            usage(console.error.bind(console));
            console.error("Invalid", arg);
            process.exit(1);
        }
    } else if (arg === "--until-event" || arg.startsWith("--until-event=")) {
        if (arg.length === 13) {
            until.event = parseInt(process.argv[++idx], 10);
        } else {
            until.event = parseInt(arg.substring(14), 10);
        }
        if (until.event <= 0 || isNaN(until.event)) {
            usage(console.error.bind(console));
            console.error("Invalid", arg);
            process.exit(1);
        }
    } else if (arg.startsWith("-") && !dashDash) {
        usage(console.error.bind(console));
        console.error("Unknown argument", arg);
        process.exit(1);
    } else if (file) {
        usage(console.error.bind(console));
        console.error("Unknown argument", arg);
    } else {
        file = arg;
    }
}

if (!file) {
    file = "/dev/stdin";
}

let contents;
try {
    contents = fs.readFileSync(file);
} catch (err) {
    usage(console.error.bind(console));
    console.error(`Failed to read file ${file} - ${err.message}`);
    process.exit(1);
}

try {
    data = JSON.parse(contents);
} catch (err) {
    usage(console.error.bind(console));
    console.error(`Failed to parse file ${file} - ${err.message}`);
    process.exit(1);
}

const model = new Model(data);
model.process(until);
