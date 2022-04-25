import prettyBytes from "pretty-bytes";
import prettyMS from "pretty-ms";
import { Stack } from "./Stack.js";

class Printer
{
    constructor(model)
    {
        this.model = model;
    }

    printPageFaultsAtStack(stack)
    {
        let byStack = this.model.pageFaultsByStack.get(stack);
        if (byStack) {
            this.model.printPageFaults(stack, byStack);
        } else {
            console.log("No page faults with stack", stack, "\n", Array.from(this.model.pageFaultsByStack.keys()));
        }
    }

    printPageFaultsAtMmapStack(stack)
    {
        let byStack = this.model.pageFaultsByMmapStack.get(stack);
        if (byStack) {
            this.model.printPageFaults(stack, byStack);
        } else {
            console.log("No page faults with mmap stack", stack);
        }
    }

    printPageFaults(stack, pageFaults)
    {
        let first = Number.MAX_SAFE_INTEGER, last = 0;
        const threads = [];
        const total = pageFaults.reduce((current, pageFault) => {
            first = Math.min(first, pageFault.time);
            last = Math.max(last, pageFault.time);
            if (threads.indexOf(pageFault.thread) === -1)
                threads.push(pageFault.thread);
            return current + pageFault.range.length;
        }, 0);

        console.log(`Got ${pageFaults.length} pageFault(s) for a total of ${prettyBytes(total)}`);
        if (pageFaults.length === 1) {
            console.log(`The page fault happened at ${prettyMS(first)} in "${this.model.data.strings[threads[0]]}"`);
        } else {
            console.log(`The page faults happened between ${prettyMS(first)} and ${last}ms in these threads: ${threads.map(x => "\"" + this.model.data.strings[x] + "\"")}`);
        }
        console.log(Stack.print(this.model.data.stacks[stack], this.model.data.strings));
    }

    pageFaultStacks()
    {
        return Array.from(this.model.pageFaultsByStack.keys());
    }

    pageFaultMmapStacks()
    {
        return Array.from(this.model.pageFaultsByMmapStack.keys());
    }

    mmaps()
    {
        return this.model.mmap;
    }

    dump()
    {
        let sorted = [];
        for (const [key, value] of this.model.pageFaultsByStack) {
            sorted.push({ stack: key, length: value.reduce((cur, x, idx) => cur + x.range.length, 0) });
        }
        sorted.sort((a, b) => a.length - b.length).forEach((x, idx) => {
            if (idx < 10) {
                this.model.printPageFaultsAtStack(x.stack);
            }
        });

    }
};

export { Printer };

