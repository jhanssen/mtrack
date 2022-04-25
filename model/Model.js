const Mmap = require("./Mmap");
const PageFault = require("./PageFault");
const Range = require("./Range");
const Stack = require("./Stack");
const assert = require("assert");
const prettyBytes = require("pretty-bytes");
const prettyMS = require("pretty-ms");

class Model
{
    constructor(data, until)
    {
        this.data = data;
        this.pageFaults = [];
        this.mmap = [];
        this.pageFaultsByStack = new Map();
        this.pageFaultsByMmapStack = new Map();
        let count = this.data.events.length;
        if (until?.event) {
            count = Math.min(until.event, count);
        }
        let time = 0;
        let pageFaultsCreated = 0;

        const removePageFaults = removeRange => {
            // return;
            // console.log("removing", removeRange);
            // this.ranges.remove(new Range(event[1], event[2], true));
            let removed = 0;
            this.pageFaults = this.pageFaults.filter(pageFault => {
                switch (removeRange.intersects(pageFault.range)) {
                case Range.Entire:
                    break;
                case Range.Before:
                case Range.After:
                    return true;
                case Range.Beginning:
                case Range.End:
                case Range.Middle:
                    throw new Error(`Partial munmap match for pageFault ${pageFault.range} ${removeRange} ${removeRange.intersects(pageFault.range)} ${pageFault.range.intersects(removeRange)}`);
                }
                let pageFaults = this.pageFaultsByStack.get(pageFault.pageFaultStack);
                let idx = pageFaults ? pageFaults.indexOf(pageFault) : -1;
                if (idx === -1) {
                    throw new Error(`Didn't find pageFault ${pageFault} in pageFaultsByStack ${Array.from(this.pageFaultsByStack.keys())}`);
                }
                if (pageFaults.length === 1) {
                    this.pageFaultsByStack.delete(pageFault.pageFaultStack);
                } else {
                    pageFaults.splice(idx, 1);
                }
                if (pageFault.mmapStack !== undefined) {
                    pageFaults = this.pageFaultsByMmapStack.get(pageFault.mmapStack);
                    idx = pageFaults ? pageFaults.indexOf(pageFault) : -1;
                    if (idx === -1) {
                        throw new Error(`Didn't find pageFault at ${pageFault.range} in pageFaultsByMmapStack with stack: ${pageFault.pageFaultStack}`);
                    }
                    if (pageFaults.length === 1) {
                        this.pageFaultsByMmapStack.delete(pageFault.pageFaultStack);
                    } else {
                        pageFaults.splice(idx, 1);
                    }
                }
                // console.log("Removing a dude", removeRange, pageFault);
                ++removed;
                return false;
            });
            return removed;
        };

        for (let idx=0; idx<count; ++idx) {
            let range;
            let mmapStack;
            let removed;
            const event = this.data.events[idx];
            switch (event[0]) {
            case Model.Time:
                time = event[1];
                if (until?.ms < time) {
                    idx = count;
                }
                break;
            case Model.MmapUntracked:
            case Model.MunmapUntracked:
            case Model.MadviseUntracked:
                break;
            case Model.MmapTracked:
                range = new Range(event[1], event[2], false);
                this.mmap.push(new Mmap(range, event[8], event[7], time));
                break;
            case Model.MunmapTracked:
                range = new Range(event[1], event[2]);
                removePageFaults(range);
                // console.log("munmap tracked removed", removed, "now we have", this.pageFaults.length);
                this.mmap = this.mmap.filter(mmap => {
                    const intersectedRange = range.intersection(mmap.range);
                    if (intersectedRange) {
                        if (intersectedRange.equals(mmap.range))
                            return false;
                        mmap.range = intersectedRange;
                    }

                    return true;
                });
                break;
            case Model.PageFault:
                range = new Range(event[1], event[2]);
                let mmapStack;
                for (let idx=this.mmap.length - 1; idx>=0; --idx) {
                    let intersection = this.mmap[idx].range.intersects(range);
                    if (intersection === Range.Entire) {
                        mmapStack = this.mmap[idx].stack;
                        break;
                    } else if (intersection >= 0) {
                        throw new Error(`Partial mmap match ${intersection} for pageFault ${range} ${this.mmap[idx].range}`);
                    }
                }
                let pageFault = new PageFault(range, event[4], mmapStack, event[3], time);
                ++pageFaultsCreated;
                let pageFaults = this.pageFaultsByStack.get(pageFault.pageFaultStack);
                this.pageFaults.push(pageFault);
                if (!pageFaults) {
                    this.pageFaultsByStack.set(pageFault.pageFaultStack, [ pageFault ]);
                } else {
                    pageFaults.push(pageFault);
                }
                if (mmapStack) {
                    pageFaults = this.pageFaultsByMmapStack.get(pageFault.mmapStack);
                    if (!pageFaults) {
                        this.pageFaultsByMmapStack.set(pageFault.mmapStack, [ pageFault ]);
                    } else {
                        pageFaults.push(pageFault);
                    }
                }
                break;
            case Model.MadviseTracked:
                removePageFaults(new Range(event[1], event[2]));
                // console.log("madvise tracked removed", removed, "now we have", this.pageFaults.length);
                break;
            default:
                throw new Error("Unknown event " + JSON.stringify(event));
            }
            // console.log("Got event", this.data.events[idx]);
        }
        // console.log(this.pageFaults.length,
        //             Array.from(this.pageFaultsByStack.keys()),
        //             Array.from(this.pageFaultsByMmapStack.keys()));
        console.log(`Loaded ${count} events spanning ${prettyMS(time)} creating ${pageFaultsCreated} pageFauls, currently ${this.pageFaults.length} are mapped in`);
    }

    printPageFaultsAtStack(stack)
    {
        let byStack = this.pageFaultsByStack.get(stack);
        if (byStack) {
            this.printPageFaults(stack, byStack);
        } else {
            console.log("No page faults with stack", stack, "\n", Array.from(this.pageFaultsByStack.keys()));
        }
    }

    printPageFaultsAtMmapStack(stack)
    {
        let byStack = this.pageFaultsByMmapStack.get(stack);
        if (byStack) {
            this.printPageFaults(stack, byStack);
        } else {
            console.log("No page faults with mmap stack", stack);
        }
    }

    printPageFaults(stack, pageFaults)
    {
        assert(pageFaults.length > 0);
        let first = Number.MAX_SAFE_INTEGER, last = 0;
        const threads = [];
        const total = pageFaults.reduce((current, pageFault) => {
            first = Math.min(first, pageFault.time);
            last = Math.max(last, pageFault.time);
            if (threads.indexOf(pageFault.thread) === -1)
                threads.push(pageFault.thread);
            return current + pageFault.range.length;
        }, 0);

        console.log(total);
        console.log(`Got ${pageFaults.length} pageFault(s) for a total of ${prettyBytes(total)}`);
        if (pageFaults.length === 1) {
            console.log(`The page fault happened at ${prettyMS(first)} in "${this.data.strings[threads[0]]}"`);
        } else {
            console.log(`The page faults happened between ${prettyMS(first)} and ${last}ms in these threads: ${threads.map(x => "\"" + this.data.strings[x] + "\"")}`);
        }
        console.log(this.data.stacks[stack]);
        console.log(this.data.strings[397]);
        console.log(Stack.print(this.data.stacks[stack], this.data.strings));
    }

    pageFaultStacks()
    {
        return Array.from(this.pageFaultsByStack.keys());
    }

    pageFaultMmapStacks()
    {
        return Array.from(this.pageFaultsByMmapStack.keys());
    }

    mmaps()
    {
        return this.mmap;
    }
}

Model.Invalid = 0;
Model.Executable = 1;
Model.Free = 2;
Model.Library = 3;
Model.LibraryHeader = 4;
Model.MadviseTracked = 5;
Model.MadviseUntracked = 6;
Model.Malloc = 7;
Model.MmapTracked = 8;
Model.MmapUntracked = 9;
Model.MunmapTracked = 10;
Model.MunmapUntracked = 11;
Model.PageFault = 12;
Model.Stack = 13;
Model.ThreadName = 14;
Model.Time = 15;
Model.WorkingDirectory = 16;


module.exports = Model;
