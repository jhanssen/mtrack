export class Range
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

    set end(val)
    {
        if (val <= this.start) {
            throw new Error(`Invalid end ${val} <= ${this.start}`);
        }
        const diff = val - this.end;
        this.length += diff;
    }

    toString()
    {
        return `${this.start}-${this.end} length: ${this.length}`;
    }

    clone()
    {
        return new Range(this.start, this.length);
    }

    intersects(range)
    {
        // this is before range
        if (this.end <= range.start) {
            return Range.Before;
        }

        // this is after range
        if (this.start >= range.end) {
            return Range.After;
        }

        // this.range overlaps the start of range
        if (this.start <= range.start) {
            if (this.end >= range.end) {
                // this.range contains all of range
                return Range.Entire;
            }
            return Range.Beginning;
        }

        if (this.end >= range.end) {
            // this.range overlaps the end of range
            return Range.End;
        }

        return Range.Middle;
    }

    // returns a new range with the intersectioning part
    intersection(range)
    {
        switch (this.intersects(range)) {
        case Range.Entire:
            return this.clone();
        case Range.Beginning:
            const ret = new Range(range.start, this.length);
            ret.end = this.end;
            return ret;
        case Range.End:
            return new Range(this.start, range.end - this.start);
        case Range.Middle:
            return range.clone();
        default:
            break;
        }
        return undefined;
    }

    equals(range)
    {
        return this.start === range.start && this.length == range.length;
    }

    remove(range)
    {
        // console.log("removing range", range, this.intersects(range));
        switch (range.intersects(this)) {
        case Range.Entire:
            // remove the whole range, return undefined
            return undefined;
        case Range.Beginning:
            // modify and return this
            this.length = range.end - this.start;
            this.start = range.end;
            return this;
        case Range.End:
            // modify and return this
            this.length = range.start - this.start;
            return this;
        case Range.Middle:
            // return a range for the parts at the start and end of range
            return [
                new Range(this.start, range.start - this.start),
                new Range(range.end, this.end - range.end)
            ];
        default:
            break;
        }
        // unchanged
        return this;
    }
};

Range.Before = -2;
Range.After = -1;
Range.Entire = 0;
Range.Beginning = 1;
Range.End = 1;
Range.Middle = 3;
