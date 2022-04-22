const assert = require("assert");

class Range
{
    constructor(start, length)
    {
        this.start = start;
        this.length = length;
    }

    get end()
    {
        return this.start + this.length;
    }

    toString()
    {
        return `${this.start}-${this.end} length: ${this.length}`;
    }

    intersects(range)
    {
        if (range.end <= this.start) {
            return Range.Before;
        }

        if (range.start >= this.end) {
            return Range.After;
        }

        // range overlaps the start of this
        if (range.start <= this.start) {
            if (range.end >= this.end) {
                return Range.Entire;
            }
            return Range.Beginning;
        }

        // range overlaps the end of this
        if (range.end >= this.end) {
            return Range.End;
        }

        return Range.Middle;
    }

    remove(range)
    {
        console.log("removing range", range, this.intersects(range));
        switch (this.intersects(range)) {
        case Range.Entire:
            return undefined;
        case Range.Beginning:
            this.length = range.end - this.start;
            this.start = range.end;
            return this;
        case Range.End:
            this.length = range.start - this.start;
            return this;
        case Range.Middle:
            return [
                new Range(this.start, range.start - this.start),
                new Range(range.end, this.end - range.end)
            ];
        default:
            break;
        }

        throw new Error(`Ranges do not intersect ${this} vs ${range}`);
    }
};

Range.Before = 1;
Range.After = 2;
Range.Entire = 3;
Range.Beginning = 4;
Range.End = 5;
Range.Middle = 6;

class Ranges
{
    constructor()
    {
        this.ranges = [];
    }

    add(range)
    {
        this.remove(range);
        // insertion sort?
        this.ranges.push(range);
        this.sort();
    }

    remove(range)
    {
        // binary search?
        let idx = 0;
        while (idx < this.ranges.length) {
            const r = this.ranges[idx];
            switch (r.intersects(range)) {
            case Range.Before:
                ++idx;
                break;
            case Range.After:
                return; // we're done
            case Range.Entire:
                this.ranges.splice(idx, 1);
                break;
            case Range.Beginning:
                this.ranges[idx] = r.remove(range);
                return;
            case Range.End:
                this.ranges[idx] = r.remove(range);
                ++idx;
                break;
            case Range.Middle:
                const removed = r.remove(range);
                this.ranges.splice(idx, 1, removed[0], removed[1]);
                return;
            }
        }
    }

    sort()
    {
        this.ranges = this.ranges.sort((l, r) => {
            if (r.start >= l.start && r.end < l.end) {
                throw new Error(`Overlapping ranges ${r} ${l}`);
            }
            if (l.start >= r.start && l.end < r.end) {
                throw new Error(`Overlapping ranges ${r} ${l}`);
            }
            return l.start - r.start;
        });
        let idx=0;
        while (idx < this.ranges.length) {

        }
    }
};

const ranges = new Ranges();
ranges.add(new Range(0, 100));
ranges.add(new Range(0, 50));

console.log(ranges);

class Model
{
    constructor(data)
    {
        this.data = data;
        this.allocationsByInstructionPointer = new Map();
    }

    process(until)
    {
        let count = this.data.events.length;
        if (until.event) {
            count = Math.min(until.event, count);
        }
        let time = 0;
        for (let idx=0; idx<count; ++idx) {
            const event = this.data.events[idx];
            switch (event[0]) {
            case Model.Time:
                this.time = event[1];
                break;
            case Model.MmapUntracked:
            case Model.MunmapUntracked:
            case Model.MadviseUntracked:
                break;
            case Model.MmapTracked:
                console.log("Got mmap tracked", event);
                break;
            case Model.MunmapTracked:
            case Model.PageFault:
            case Model.MadviseTracked:
                break;
            default:
                throw new Error("Unknown event " + JSON.stringify(event));
            }
            // console.log("Got event", this.data.events[idx]);
        }
        // console.log(this.strings);
        // console.log(this.stacks);
        // console.log(until, Object.keys(this.data));
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
