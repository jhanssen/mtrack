import { Model } from "./Model.js";
import { ModelOptions } from "./ModelOptions";
import { Printer } from "./Printer.js";
import { Until } from "./Until";
import fs from "fs";
import os from "os";
import path from "path";
import readline, { CompleterResult } from "readline";

function usage(out: (...arg: unknown[]) => void): void {
    out(`${process.argv[0]} ${process.argv[1]} (--help|-h) (--until-ms <ms>) (--until-event <event>) (--verbose|-v) (--silent) <mtrack.json>`);
}

let dashDash = false;
let file;
let data;
const until: Until = {};
const options: ModelOptions = {};

for (let idx=2; idx<process.argv.length; ++idx) {
    const arg = process.argv[idx];
    if (arg === "--help" || arg === "-h") {
        usage(console.log.bind(console));
        process.exit(0);
    } else if (arg === "--verbose" || arg === "-v") {
        options.verbose = true;
        options.silent = false;
    } else if (arg === "--silent") {
        options.verbose = false;
        options.silent = true;
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

let contents: string | undefined;
try {
    contents = fs.readFileSync(file, "utf8");
} catch (err: unknown) {
    usage(console.error.bind(console));
    console.error(`Failed to read file ${file} - ${(err as Error).message}`);
    process.exit(1);
}

try {
    data = JSON.parse(contents);
} catch (err) {
    usage(console.error.bind(console));
    console.error(`Failed to parse file ${file} - ${(err as Error).message}`);
    process.exit(1);
}

const model = new Model(data, options);
const parsed = model.parse(until);
const printer = new Printer(model, parsed);

// let history: undefined | string[];
// try {
//     history = fs.readFileSync(path.join(os.homedir(), ".mtrack-model-history"), "utf8").split("\n");
// } catch (err) {
//     /* */
// }

const completions = "help h ? quit q stacks s sm stacksMmap dump d pf mmaps m printPageFaults pfm printPageFaultsByMmap".split(" ");
function completer(line: string): CompleterResult {
    const split = line.split(" ").filter(x => x);
    if (split.length === 1) {
        switch (split[0]) {
        case "pf":
        case "printPageFaults":
            return [model.allPageFaultStacks().map(x => `${split[0]} ${x}`), line];
        case "pfm":
        case "printPageFaultsByMmap":
            return [model.allMmapStacks().map(x => `${split[0]} ${x}`), line];
        }
    }
    const hits = completions.filter((c) => c.startsWith(line));
    return [hits.length ? hits : completions, line];
}

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    // history,
    completer,
    removeHistoryDuplicates: true
});

rl.on("history", historyArray => {
    try {
        fs.writeFileSync(path.join(os.homedir(), ".mtrack-model-history"), historyArray.join("\n"));
    } catch (err) {
        /**/
    }
});

function prompt(): void {
    rl.question("$ ", (query: string) => {
        const input = query.split(" ").map(x => x.trim()).filter(x => x);
        const help = "help|h|?\nq|quit\ns|stacks\nsm stacksMmap|mmaps|m\ndump|d\npf|printPageFaults <stackid>\npfm|printPageFaultsAtMmapStack <stackid>";
        switch (input[0]) {
        case "help":
        case "h":
        case "?":
            console.log(help);
            break;
        case "q":
        case "quit":
            process.exit();
        case "mmaps":
        case "m":
            console.log(printer.mmaps());
            break;
        case "s":
        case "stacks": {
            let line: number[] = [];
            printer.pageFaultStacks().forEach((x: number) => {
                line.push(x);
                if (line.length === 8) {
                    console.log(line.join("\t"));
                    line = [];
                }
            });
            if (line.length) {
                console.log(line.join("\t"));
            }
            break; }
        case "sm":
        case "stacksMmap": {
            let line: number[] = [];
            printer.pageFaultMmapStacks().forEach((x: number) => {
                line.push(x);
                if (line.length === 8) {
                    console.log(line.join("\t"));
                    line = [];
                }
            });
            if (line.length) {
                console.log(line.join("\t"));
            }
            break; }
        case "dump":
        case "d":
            printer.dump();
            break;
        case "pf":
        case "printPageFaults":
            if (isNaN(parseInt(input[1]))) {
                console.error("Invalid stack id", input[1]);
                break;
            }

            printer.printPageFaultsAtStack(parseInt(input[1]));
            break;
        case "pfm":
        case "printPageFaultsByMmap":
            if (isNaN(parseInt(input[1]))) {
                console.error("Invalid stack id", input[1]);
                break;
            }
            printer.printPageFaultsAtMmapStack(parseInt(input[1]));
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



