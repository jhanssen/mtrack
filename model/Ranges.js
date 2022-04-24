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
        this.ranges = this.ranges.sort((l, r) => {
            if (r.start >= l.start && r.end < l.end) {
                throw new Error(`Overlapping ranges ${r} ${l}`);
            }
            if (l.start >= r.start && l.end < r.end) {
                throw new Error(`Overlapping ranges ${r} ${l}`);
            }
            return l.start - r.start;
        });
        let idx=1;
        while (idx < this.ranges.length) {
            const prev = this.ranges[idx - 1];
            const cur = this.ranges[idx];
            if (prev.end == cur.start) {
                // console.log("joining", prev, cur);
                prev.length += cur.length;
                this.ranges.splice(idx);
            } else {
                ++idx;
            }
        }
    }

    remove(range)
    {
        let removed = [];
        // binary search?
        let idx = 0;
        while (idx < this.ranges.length) {
            const r = this.ranges[idx];
            switch (r.intersects(range)) {
            case Range.Before:
                ++idx;
                break;
            case Range.After:
                return removed; // we're done
            case Range.Entire:
                removed.push(this.ranges.splice(idx, 1));
                break;
            case Range.Beginning:
                this.ranges[idx] = r.remove(range);
                return removed;
            case Range.End:
                removed.
                    this.ranges[idx] = r.remove(range);
                ++idx;
                break;
            case Range.Middle:
                const removed = r.remove(range);
                this.ranges.splice(idx, 1, removed[0], removed[1]);
                return undefined;
            }
        }
        return removed;
    }
};

// const ranges = new Ranges();
// ranges.add(new Range(0, 100));
// ranges.add(new Range(0, 50));
// ranges.remove(new Range(0, 100));

// console.log(ranges);

module.exports = Ranges;
