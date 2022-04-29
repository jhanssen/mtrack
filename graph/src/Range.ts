import { IntersectionType } from "./IntersectionType";

export class Range {
    start: number;
    length: number;

    constructor(start: number, length: number) {
        this.start = start;
        this.length = length;
    }

    get end(): number {
        return this.start + this.length;
    }

    set end(val: number) {
        if (val <= this.start) {
            throw new Error(`Invalid end ${val} <= ${this.start}`);
        }
        const diff = val - this.end;
        this.length += diff;
    }

    toString(): string {
        return `${this.start}-${this.end} length: ${this.length}`;
    }

    clone(): Range {
        return new Range(this.start, this.length);
    }

    intersects(range: Range): IntersectionType {
        // this is before range
        if (this.end <= range.start) {
            return IntersectionType.Before;
        }

        // this is after range
        if (this.start >= range.end) {
            return IntersectionType.After;
        }

        // this.range overlaps the start of range
        if (this.start <= range.start) {
            if (this.end >= range.end) {
                // this.range contains all of range
                return IntersectionType.Entire;
            }
            return IntersectionType.Beginning;
        }

        if (this.end >= range.end) {
            // this.range overlaps the end of range
            return IntersectionType.End;
        }

        return IntersectionType.Middle;
    }

    // returns a new range with the intersectioning part
    intersection(range: Range): Range | undefined {
        switch (this.intersects(range)) {
        case IntersectionType.Entire:
            return this.clone();
        case IntersectionType.Beginning: {
            const ret = new Range(range.start, this.length);
            ret.end = this.end;
            return ret; }
        case IntersectionType.End:
            return new Range(this.start, range.end - this.start);
        case IntersectionType.Middle:
            return range.clone();
        default:
            break;
        }
        return undefined;
    }

    equals(range: Range): boolean {
        return this.start === range.start && this.length === range.length;
    }

    remove(range: Range): Range | [Range, Range] | undefined {
        // console.log("removing range", range, this.intersects(range));
        switch (range.intersects(this)) {
        case IntersectionType.Entire:
            // remove the whole range, return undefined
            return undefined;
        case IntersectionType.Beginning:
            // modify and return this
            this.length = range.end - this.start;
            this.start = range.end;
            return this;
        case IntersectionType.End:
            // modify and return this
            this.length = range.start - this.start;
            return this;
        case IntersectionType.Middle:
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
}
