import { Data } from "./Data";
import { IntersectionType } from "./IntersectionType";
import { Malloc } from "./Malloc.js";
import { Mmap } from "./Mmap.js";
import { ModelOptions } from "./ModelOptions";
import { PageFault } from "./PageFault.js";
import { ParseResult } from "./ParseResult";
import { Range } from "./Range.js";
import { RecordType } from "./RecordType";
import { Snapshot } from "./Snapshot.js";
import { Stack } from "./Stack";
import { Until } from "./Until";

export class Model {
    private readonly options: ModelOptions;
    private readonly error: (...arg: unknown[]) => void;
    private readonly log: (...arg: unknown[]) => void;
    private readonly verbose: (...arg: unknown[]) => void;
    private pageFaults?: PageFault[];
    private pageFaultsByStack?: Map<number, PageFault[]>;
    private pageFaultsByMmapStack?: Map<number, PageFault[]>;
    private mallocsByStack?: Map<number, Malloc[]>;
    private mallocsByAddress?: Map<number, Malloc>;
    private memmaps?: Mmap[];

    readonly data: Data;
    constructor(data: Data, options?: ModelOptions) {
        if (!data || typeof data !== "object") {
            throw new Error(`Invalid data ${data}`);
        }
        this.options = options || { silent: true };
        this.data = data;
        if (this.options.silent) {
            this.log = this.error = this.verbose = () => { /* */ };
        } else {
            this.error = console.error.bind(console);
            this.log = console.log.bind(console);
            if (this.options.verbose) {
                this.verbose = this.log;
            } else {
                this.verbose = () => { /* */ };
            }
        }
    }

    get mmaps(): Mmap[] {
        if (!this.memmaps) {
            throw new Error("Not parsed");
        }
        return this.memmaps;
    }

    printStack(stack: number): string {
        if (!this.data) {
            throw new Error("Not parsed");
        }
        return Stack.print(this.data.stacks[stack], this.data.strings);
    }

    sortedStacks(max?: number): number[] {
        if (!this.pageFaultsByStack) {
            throw new Error("Not parsed");
        }

        type Sortable = { stack: number, length: number };
        let sorted: Sortable[] = [];
        for (const [key, value] of this.pageFaultsByStack) {
            sorted.push({ stack: key, length: value.reduce((cur: number, x: PageFault) => cur + x.range.length, 0) });
        }
        sorted = sorted.sort((a, b) => b.length - a.length);
        if (max) {
            sorted.length = Math.min(max, sorted.length);
        }
        return sorted.map((x: Sortable) => x.stack);
    }

    pageFaultsAtStack(stack: number): PageFault[] | undefined {
        if (!this.pageFaultsByStack) {
            throw new Error("Not parsed");
        }
        return this.pageFaultsByStack.get(stack);
    }

    pageFaultsAtMmapStack(stack: number): PageFault[] | undefined {
        if (!this.pageFaultsByMmapStack) {
            throw new Error("Not parsed");
        }
        return this.pageFaultsByMmapStack.get(stack);
    }

    allPageFaultStacks(): number[] {
        if (!this.pageFaultsByStack) {
            throw new Error("Not parsed");
        }
        return Array.from(this.pageFaultsByStack.keys());
    }

    allMmapStacks(): number[] {
        if (!this.pageFaultsByMmapStack) {
            throw new Error("Not parsed");
        }
        return Array.from(this.pageFaultsByMmapStack.keys());
    }


    parse(until?: Until, callback?: (snapshot: Snapshot) => void): ParseResult {
        this.pageFaults = [];
        this.memmaps = [];
        this.pageFaultsByStack = new Map();
        this.pageFaultsByMmapStack = new Map();
        this.mallocsByStack = new Map();
        this.mallocsByAddress = new Map();

        let count = this.data.events.length;
        if (until?.event) {
            count = Math.min(until.event, count);
        }
        let time = 0;
        let lastSnapshotTime = 0;
        let currentMemoryUsage = 0;
        let pageFaultsCreated = 0;

        if (this.data.events[count - 1] !== null) {
            throw new Error("Expected null, got " + JSON.stringify(this.data.events[count - 1]));
        }
        --count;
        const tenPercent = Math.floor(count / 10);
        for (let idx=0; idx<count - 1; ++idx) {
            const event = this.data.events[idx];
            switch (event[0]) {
            case RecordType.Time:
                if (idx > 0 && callback) {
                    lastSnapshotTime = time;
                    callback(new Snapshot(currentMemoryUsage, time, idx));
                }
                time = event[1];
                if (until?.ms || 0 < time) {
                    idx = count;
                }
                break;
            case RecordType.MmapUntracked:
            case RecordType.MunmapUntracked:
            case RecordType.MadviseUntracked:
                break;
            case RecordType.Malloc: {
                const malloc = new Malloc(new Range(event[1], event[2]), event[3], event[4], time);
                this.mallocsByAddress.set(malloc.range.start, malloc);
                const current = this.mallocsByStack.get(malloc.stack);
                if (!current) {
                    this.mallocsByStack.set(malloc.stack, [malloc]);
                } else {
                    current.push(malloc);
                }
                break; }
            case RecordType.Free: {
                const malloc = this.mallocsByAddress.get(event[1]);
                if (!malloc) {
                    this.error("Can't find malloc for event", event);
                    break;
                }
                this.mallocsByAddress.delete(event[1]);
                const current = this.mallocsByStack.get(malloc.stack);
                if (!current) {
                    throw new Error("Can't find malloc by stack: " + malloc);
                }
                if (current.length === 1) {
                    this.mallocsByStack.delete(malloc.range.start);
                } else {
                    current.splice(current.indexOf(malloc), 1);
                }
                break; }
            case RecordType.MmapTracked: {
                const range = new Range(event[1], event[2]);
                this.memmaps.push(new Mmap(range, event[8], event[7], time));
                break; }
            case RecordType.MunmapTracked: {
                const range = new Range(event[1], event[2]);
                currentMemoryUsage -= this.removePageFaults(range);
                // console.log("munmap tracked removed", removed, "now we have", this.pageFaults.length);
                let i = 0;
                while (i < this.memmaps.length) {
                    const ret = this.memmaps[i].range.remove(range);
                    if (!ret) {
                        this.memmaps.splice(i, 1);
                        continue;
                    }
                    if (Array.isArray(ret)) {
                        if (!this.memmaps) {
                            throw new Error("Must have mmaps");
                        }
                        const newMmaps = ret.map((range: Range) => (this.memmaps as Mmap[])[i].clone(range));
                        this.memmaps.splice(idx, 1, newMmaps[0], newMmaps[1]);
                        i += 2;
                    } else {
                        ++i;
                    }
                }
                break; }
            case RecordType.PageFault: {
                const range = new Range(event[1], event[2]);
                let mmapStack: undefined | number;
                for (let idx=this.memmaps.length - 1; idx>=0; --idx) {
                    const intersection = this.memmaps[idx].range.intersects(range);
                    if (intersection === IntersectionType.Entire) {
                        mmapStack = this.memmaps[idx].stack;
                        break;
                    } else if (intersection >= 0) {
                        throw new Error(`Partial mmap match ${intersection} for pageFault ${range} ${this.memmaps[idx].range}`);
                    }
                }
                const pageFault = new PageFault(range, event[4], mmapStack, event[3], time);
                ++pageFaultsCreated;
                let pageFaults = this.pageFaultsByStack.get(pageFault.stack);
                this.pageFaults.push(pageFault);
                currentMemoryUsage += pageFault.range.length;
                if (!pageFaults) {
                    this.pageFaultsByStack.set(pageFault.stack, [ pageFault ]);
                } else {
                    pageFaults.push(pageFault);
                }
                if (mmapStack !== undefined) {
                    pageFaults = this.pageFaultsByMmapStack.get(mmapStack);
                    if (!pageFaults) {
                        this.pageFaultsByMmapStack.set(mmapStack, [ pageFault ]);
                    } else {
                        pageFaults.push(pageFault);
                    }
                }
                break; }
            case RecordType.MadviseTracked:
                currentMemoryUsage -= this.removePageFaults(new Range(event[1], event[2]));
                // console.log("madvise tracked removed", removed, "now we have", this.pageFaults.length);
                break;
            default:
                throw new Error("Unknown event " + JSON.stringify(event));
            }
            // console.log("Got event", this.data.events[idx]);
            if (idx % tenPercent === 0 && !this.options.silent) {
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

    private removePageFaults(removeRange: Range): number {
        // return;
        // console.log("removing", removeRange);
        // this.ranges.remove(new Range(event[1], event[2], true));
        let removed = 0;
        if (!this.pageFaults) {
            throw new Error("Not parsed");
        }
        this.pageFaults = this.pageFaults.filter(pageFault => {
            switch (removeRange.intersects(pageFault.range)) {
            case IntersectionType.Entire:
                break;
            case IntersectionType.Before:
            case IntersectionType.After:
                return true;
            case IntersectionType.Beginning:
            case IntersectionType.End:
            case IntersectionType.Middle:
                throw new Error(`Partial munmap match for pageFault ${pageFault.range} ${removeRange} ${removeRange.intersects(pageFault.range)} ${pageFault.range.intersects(removeRange)}`);
            }
            if (!this.pageFaultsByStack) {
                throw new Error("WTF?");
            }
            let pageFaults = this.pageFaultsByStack.get(pageFault.stack);
            let idx = pageFaults ? pageFaults.indexOf(pageFault) : -1;
            if (idx === -1 || !pageFaults) {
                throw new Error(`Didn't find pageFault ${pageFault} in pageFaultsByStack ${Array.from(this.pageFaultsByStack.keys())}`);
            }
            if (pageFaults.length === 1) {
                this.pageFaultsByStack.delete(pageFault.stack);
            } else {
                pageFaults.splice(idx, 1);
            }
            removed += pageFault.range.length;
            if (pageFault.mmapStack !== undefined) {
                if (!this.pageFaultsByMmapStack) {
                    throw new Error("WTF?");
                }
                pageFaults = this.pageFaultsByMmapStack.get(pageFault.mmapStack);
                idx = pageFaults ? pageFaults.indexOf(pageFault) : -1;
                if (idx === -1 || !pageFaults) {
                    throw new Error(`Didn't find pageFault at ${pageFault.range} in pageFaultsByMmapStack with stack: ${pageFault.stack}`);
                }
                if (pageFaults.length === 1) {
                    this.pageFaultsByMmapStack.delete(pageFault.stack);
                } else {
                    pageFaults.splice(idx, 1);
                }
            }
            // console.log("Removing a dude", removeRange, pageFault);
            return false;
        });
        return removed;
    }
}
