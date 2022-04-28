import { Malloc } from "./Malloc.js";
import { Mmap } from "./Mmap.js";
import { PageFault } from "./PageFault.js";
import { Range } from "./Range.js";
import { Snapshot } from "./Snapshot.js";

export class Model
{
    constructor(data, options)
    {
        if (!data || typeof data !== "object") {
            throw new Error(`Invalid data ${data}`);
        }
        this.options = options || { silent: true };
        this.data = data;
        if (this.options.silent) {
            this.log = this.error = this.verbose = () => {};
        } else {
            this.error = console.error.bind(console);
            this.log = console.log.bind(console);
            if (this.options.verbose) {
                this.verbose = this.log;
            } else {
                this.verbose = () => {};
            }
        }
    }

    parse(until, callback)
    {
        this.pageFaults = [];
        this.mmaps = [];
        this.pageFaultsByStack = new Map();
        this.pageFaultsByMmapStack = new Map();
        this.mallocsByStack = new Map();
        this.mallocsByAddr = new Map();

        let count = this.data.events.length;
        if (until?.event) {
            count = Math.min(until.event, count);
        }
        let time = 0;
        let lastSnapshotTime = 0;
        let currentMemoryUsage = 0;
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
                currentMemoryUsage -= pageFault.range.length;
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

        if (this.data.events[count - 1] !== null) {
            throw new Error("Expected null, got " + JSON.stringify(this.data.events[count - 1]));
        }
        --count;
        const tenPercent = Math.floor(count / 10);
        for (let idx=0; idx<count - 1; ++idx) {
            let range;
            let mmapStack;
            let removed;
            let current;
            let malloc;
            const event = this.data.events[idx];
            switch (event[0]) {
            case Model.Time:
                if (idx > 0 && callback) {
                    lastSnapshotTime = time;
                    callback(new Snapshot(currentMemoryUsage, time, idx));
                }
                time = event[1];
                if (until?.ms < time) {
                    idx = count;
                }
                break;
            case Model.MmapUntracked:
            case Model.MunmapUntracked:
            case Model.MadviseUntracked:
                break;
            case Model.Malloc:
                malloc = new Malloc(new Range(event[1], event[2]), event[3], event[4]);
                this.mallocsByAddr.set(malloc.range.start, malloc);
                current = this.mallocsByStack.get(malloc.stack);
                if (!current) {
                    this.mallocsByStack.set(malloc.stack, [malloc]);
                } else {
                    current.push(malloc);
                }
                break;
            case Model.Free:
                malloc = this.mallocsByAddr.get(event[1]);
                if (!malloc) {
                    this.error("Can't find malloc for event", event);
                    break;
                }
                this.mallocsByAddr.delete(event[1]);
                current = this.mallocsByStack.get(malloc.stack);
                if (current.length === 1) {
                    this.mallocsByStack.delete(malloc.range.start);
                } else {
                    current.splice(current.indexOf(malloc), 1);
                }
                break;
            case Model.MmapTracked:
                range = new Range(event[1], event[2], false);
                this.mmaps.push(new Mmap(range, event[8], event[7], time));
                break;
            case Model.MunmapTracked:
                range = new Range(event[1], event[2]);
                removePageFaults(range);
                // console.log("munmap tracked removed", removed, "now we have", this.pageFaults.length);
                let i = 0;
                while (i < this.mmaps.length) {
                    const ret = this.mmaps[i].range.remove(range);
                    if (!ret) {
                        this.mmaps.splice(i, 1);
                        continue;
                    }
                    if (Array.isArray(ret)) {
                        const newMmaps = ret.map(range => this.mmaps[i].clone(range));
                        this.mmaps.splice(idx, 1, newMmaps[0], newMmaps[1]);
                        i += 2;
                    } else {
                        ++i;
                    }
                }
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
                        throw new Error(`Partial mmap match ${intersection} for pageFault ${range} ${this.mmaps[idx].range}`);
                    }
                }
                let pageFault = new PageFault(range, event[4], mmapStack, event[3], time);
                ++pageFaultsCreated;
                let pageFaults = this.pageFaultsByStack.get(pageFault.pageFaultStack);
                this.pageFaults.push(pageFault);
                currentMemoryUsage += pageFault.range.length;
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
            if (idx % tenPercent  === 0 && !this.options.silent) {
                this.log(`Parsed ${idx}/${count} ${((idx / count) * 100).toFixed(2)}%`);
            }

        }
        if (callback && time !== lastSnapshotTime) {
            callback(new Snapshot(currentMemoryUsage, time, count));
        }
        // console.log(this.pageFaults.length,
        //             Array.from(this.pageFaultsByStack.keys()),
        //             Array.from(this.pageFaultsByMmapStack.keys()));
        return {
            events: count,
            pageFaults: pageFaultsCreated,
            mapped: this.pageFaults.length,
            time
        };
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
Model.ThreadName = 13;
Model.Time = 14;
Model.WorkingDirectory = 15;
