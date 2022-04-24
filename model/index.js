#!/usr/bin/env node

const Model = require("Model");
const fs = require("fs");
const readline = require("readline");

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

const model = new Model(data, until);

const rl = readline.createInterface({ input: process.stdin, output: process.stdout });

function prompt()
{
    rl.question("$ ", undefined, input => {
        input = input.split(" ").map(x => x.trim()).filter(x => x);
        const help = "help|h|?\nq|quit\ns|stacks\npf|printPageFaults <stackid>\npfm|printPageFaultsAtMmapStack <stackid>";
        switch (input[0]) {
        case "help":
        case "h":
        case "?":
            console.log(help);
            break;
        case "q":
        case "quit":
            process.exit();
        case "s":
        case "stacks":
            console.log(model.stacks().join("\n"));
            break;
        case "pf":
        case "printPageFaults":
            if (isNaN(parseInt(input[1]))) {
                console.error("Invalid stack id", input[1]);
                break;
            }

            model.printPageFaultsAtStack(parseInt(input[1]));
            break;
        case "pfm":
        case "printPageFaultsByMmap":
            if (isNaN(parseInt(input[1]))) {
                console.error("Invalid stack id", input[1]);
                break;
            }
            model.printPageFaultsAtMmapStack(parseInt(input[1]));
            break;
        default:
            console.error("Invalid command", input[0], "\n", help);
            break;
        }
        // console.log(input);
        prompt();
    });
}

prompt();

