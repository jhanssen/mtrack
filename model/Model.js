const assert = require("assert");
const Range = require("./Range");
const PageFault = require("./PageFault");
const Mmap = require("./Mmap");

class Model
{
    constructor(data)
    {
        this.data = data;
        this.pageFaults = [];
        this.mmaps = [];
        this.allocationsByPageFaultStack = new Map();
        this.allocationsByMmapStack = new Map();
    }

    process(until)
    {
        let count = this.data.events.length;
        if (until.event) {
            count = Math.min(until.event, count);
        }
        let time = 0;
        for (let idx=0; idx<count; ++idx) {
            let range;
            let mmapStack;
            const event = this.data.events[idx];
            switch (event[0]) {
            case Model.Time:
                time = event[1];
                break;
            case Model.MmapUntracked:
            case Model.MunmapUntracked:
            case Model.MadviseUntracked:
                break;
            case Model.MmapTracked:
                range = new Range(event[1], event[2], false);
                this.mmaps.push(new Mmap(range, event[8], event[7], time));
                break;
            case Model.MunmapTracked:
                range = new Range(event[1], event[2]);
                this.removePageFaults(range);
                this.mmaps = this.mmaps.filter(mmap => {
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
                for (let idx=this.mmaps.length - 1; idx>=0; --idx) {
                    let intersection = this.mmaps[idx].range.intersects(range);
                    if (intersection === Range.Entire) {
                        mmapStack = this.mmaps[idx].stack;
                        break;
                    } else if (intersection >= 0) {
                        this.mmaps[idx].range.intersects(range, true);
                        throw new Error(`Partial mmap match ${intersection} for pageFault ${range} ${this.mmaps[idx].range}`);
                    }
                }
                let pageFault = new PageFault(range, event[4], mmapStack, event[3], time);
                let allocations = this.allocationsByPageFaultStack.get(pageFault.pageFaultStack);
                this.pageFaults.push(pageFault);
                if (!allocations) {
                    this.allocationsByPageFaultStack.set(pageFault.pageFaultStack, [ pageFault ]);
                } else {
                    allocations.push(pageFault);
                }
                if (mmapStack) {
                    allocations = this.allocationsByMmapStack.get(pageFault.mmapStack);
                    if (!allocations) {
                        this.allocationsByMmapStack.set(pageFault.mmapStack, [ pageFault ]);
                    } else {
                        allocations.push(pageFault);
                    }
                }
                break;
            case Model.MadviseTracked:
                this.removePageFaults(new Range(event[1], event[2]));
                break;
            default:
                throw new Error("Unknown event " + JSON.stringify(event));
            }
            // console.log("Got event", this.data.events[idx]);
        }
        // console.log(this.strings);
        // console.log(this.stacks);
        // console.log(until, Object.keys(this.data));
        // console.log(this.ranges);
    }

    removePageFaults(range)
    {
        // console.log("removing", event[1], event[2], idx);
        // this.ranges.remove(new Range(event[1], event[2], true));
        this.pageFaults = this.pageFaults.filter(pageFault => {
            switch (range.intersects(pageFault.range)) {
            case Range.Entire:
                break;
            case Range.Before:
            case Range.After:
                return true;
            case Range.Beginning:
            case Range.End:
            case Range.Middle:
                throw new Error(`Partial munmap match for pageFault ${pageFault.range} ${range} ${range.intersects(pageFault.range)} ${pageFault.range.intersects(range)}`);
            }
            let allocations = this.allocationsByPageFaultStack.get(pageFault.pageFaultStack);
            let idx = allocations ? allocations.indexOf(pageFault) : -1;
            if (idx === -1) {
                throw new Error(`Didn't find pageFault ${pageFault} in allocationsByPageFaultStack ${Array.from(this.allocationsByPageFaultStack.keys())}`);
            }
            if (allocations.length === 1) {
                this.allocationsByPageFaultStack.delete(pageFault.pageFaultStack);
            } else {
                allocations.splice(idx, 1);
            }
            if (pageFault.mmapStack !== undefined) {
                allocations = this.allocationsByMmapStack.get(pageFault.mmapStack);
                idx = allocations ? allocations.indexOf(pageFault) : -1;
                if (idx === -1) {
                    throw new Error(`Didn't find pageFault at ${pageFault.range} in allocationsByMmapStack with stack: ${pageFault.pageFaultStack}`);
                }
                if (allocations.length === 1) {
                    this.allocationsByMmapStack.delete(pageFault.pageFaultStack);
                } else {
                    allocations.splice(idx, 1);
                }

            }
            return false;
        });
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
