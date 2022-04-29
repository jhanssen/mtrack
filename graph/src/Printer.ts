import { Mmap } from "./Mmap";
import { Model } from "./Model";
import { PageFault } from "./PageFault";
import { ParseResult } from "./ParseResult";
import { Stack } from "./Stack.js";
import prettyBytes from "pretty-bytes";
import prettyMS from "pretty-ms";

export class Printer {
    readonly model: Model;

    constructor(model: Model, parsed: ParseResult) {
        this.model = model;
        console.log(`Loaded ${parsed.events} events spanning ${prettyMS(parsed.time)} creating ${parsed.pageFaults} pageFaults, currently ${parsed.mapped} are mapped in`);
    }

    printPageFaultsAtStack(stack: number): void {
        const byStack = this.model.pageFaultsAtStack(stack);
        if (byStack) {
            this.printPageFaults(stack, byStack);
        } else {
            console.log("No page faults with stack", stack, "\n", this.model.allPageFaultStacks());
        }
    }

    printPageFaultsAtMmapStack(stack: number): void {
        const byStack = this.model.pageFaultsAtMmapStack(stack);
        if (byStack) {
            this.printPageFaults(stack, byStack);
        } else {
            console.log("No page faults with mmap stack", stack, "\n", this.model.allMmapStacks());
        }
    }

    printPageFaults(stack: number, pageFaults: PageFault[]): void {
        let first = Number.MAX_SAFE_INTEGER, last = 0;
        const threads: number[] = [];
        const total = pageFaults.reduce((current, pageFault) => {
            first = Math.min(first, pageFault.time);
            last = Math.max(last, pageFault.time);
            if (threads.indexOf(pageFault.thread) === -1) {
                threads.push(pageFault.thread);
            }
            return current + pageFault.range.length;
        }, 0);

        console.log(`Got ${pageFaults.length} pageFault(s) for a total of ${prettyBytes(total)}`);
        if (pageFaults.length === 1) {
            console.log(`The page fault happened at ${prettyMS(first)} in "${this.model.data.strings[threads[0]]}"`);
        } else {
            console.log(`The page faults happened between ${prettyMS(first)} and ${last}ms in these thread(s): ${threads.map(x => "\"" + this.model.data.strings[x] + "\"").join("\n")}`);
        }
        console.log(Stack.print(this.model.data.stacks[stack], this.model.data.strings));
    }

    pageFaultStacks(): number[] {
        return this.model.allPageFaultStacks();
    }

    pageFaultMmapStacks(): number[] {
        return this.model.allMmapStacks();
    }

    mmaps(): Mmap[] {
        return this.model.mmaps;
    }

    dump(): void {
        this.model.sortedStacks(10).forEach((stack: number, idx: number) => {
            console.log(`---------------- ${idx} ----------------`);
            this.printPageFaultsAtStack(stack);
        });
    }
}
