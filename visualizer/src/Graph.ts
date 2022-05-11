import "d3-transition";
import { FlameGraph, flamegraph } from "d3-flame-graph";
import { Line, ScaleLinear, axisBottom, axisLeft, easeCubic, extent, line, max, scaleLinear, select } from "d3";
import { Model2, PageSize, Snapshot } from "./Model2";
import { Stack } from "./Stack";
import { assert } from "./Assert";

type Margin = {
    top: number;
    right: number;
    bottom: number;
    left: number;
};

type LineData = {
    margin: Margin;
    width: number;
    height: number;
    x: ScaleLinear<number, number>;
    y: ScaleLinear<number, number>;
    svg: unknown;
    valueLine: Line<[number, number]>;
};

type Ready = {
    resolve: () => void | PromiseLike<void>;
    reject: (reason?: unknown) => void;
};

type GraphChild = { name: string, value: number, ip: number, children: GraphChild[] };
type GraphSnapshot = { name: string, value: number, children: GraphChild[] };

interface WalkEntry {
    ip: number;
    child?: GraphChild;
}

interface WalkStack {
    hash: number;
    entries: WalkEntry[];
}

declare class DecompressionStream {
    constructor(format: string);

    readonly readable: ReadableStream<BufferSource>;
    readonly writable: WritableStream<Uint8Array>;
}

// lifted from https://stackoverflow.com/questions/7616461/generate-a-hash-from-string-in-javascript
function cyrb53(str: string, seed: number = 0) {
    let h1 = 0xdeadbeef ^ seed, h2 = 0x41c6ce57 ^ seed;
    for (let i = 0, ch; i < str.length; i++) {
        ch = str.charCodeAt(i);
        h1 = Math.imul(h1 ^ ch, 2654435761);
        h2 = Math.imul(h2 ^ ch, 1597334677);
    }
    h1 = Math.imul(h1 ^ (h1>>>16), 2246822507) ^ Math.imul(h2 ^ (h2>>>13), 3266489909);
    h2 = Math.imul(h2 ^ (h2>>>16), 2246822507) ^ Math.imul(h1 ^ (h1>>>13), 3266489909);
    return 4294967296 * (2097151 & h2) + (h1>>>0);
}

export class Graph {
    private _line: LineData;
    private _flame: FlameGraph;
    private _model: Model2 | undefined;
    private _nomodel: unknown | undefined;
    private _readies: Ready[] = [];
    private _prevSnapshot: number | undefined;

    constructor() {
        const margin = {top: 20, right: 20, bottom: 50, left: 70};
        const width = 750 - margin.left - margin.right;
        const height = 400 - margin.top - margin.bottom;

        const x = scaleLinear().range([0, width]);
        const y = scaleLinear().range([height, 0]);

        const valueLine = line()
            // @ts-ignore
            .x(d => x(d.time))
            // @ts-ignore
            .y(d => y(d.used));

        const svg = select("#linechart")
              .append("svg")
              .attr("width", width + margin.left + margin.top)
              .attr("height", height + margin.top + margin.bottom)
              .append("g")
              .attr("transform", `translate(${margin.left},${margin.top})`);

        this._line = {
            margin, width, height, x, y, svg, valueLine
        };

        this._flame = flamegraph()
            .width(1920)
            .cellHeight(18)
            .transitionDuration(750)
            .minFrameSize(5)
            // flamegraph types are wrong
            .transitionEase(easeCubic as unknown as string)
            .sort(true)
            .title("")
            //.onClick(onClick)
            .selfValue(false);

        const data = "data:application/octet-binary;base64,$DATA_GOES_HERE$";
        try {
            window.fetch(data)
                .then(res => res.blob())
                .then(blob => {
                    const ds = new DecompressionStream("gzip");
                    const decompressedStream = blob.stream().pipeThrough(ds);
                    return new Response(decompressedStream).blob();
                })
                .then(blob => blob.arrayBuffer())
                .then(buffer => {
                    console.log("got decompressed", buffer.byteLength);
                    this._model = new Model2(buffer);
                    for (const r of this._readies) {
                        r.resolve();
                    }
                    this._readies = [];
                }).catch(e => {
                    this._nomodel = e;
                    for (const r of this._readies) {
                        r.reject(e);
                    }
                    this._readies = [];
                });
        } catch (e) {
            console.error("fetch??", e);
        }
        //this._model = new Model(this._data);
    }

    ready() {
        return new Promise<void>((resolve, reject) => {
            if (this._model) {
                resolve();
            } else if (this._nomodel !== undefined) {
                reject(this._nomodel);
            } else {
                this._readies.push({ resolve, reject });
            }
        });
    }

    init() {
        if (this._model === undefined) {
            throw new Error("init called with no model");
        }

        interface StackData {
            time: number;
            used: number;
            name?: string;
        }

        const mdata_t: { time: number, used: number }[] = [];
        const mdata_p: { time: number, used: number }[] = [];
        const mdata_m: { time: number, used: number }[] = [];
        const sdata_t: StackData[] = [];
        this._model.parse();
        for (const memory of this._model.memories) {
            mdata_t.push({ time: memory.time, used: (memory.pageFault + memory.malloc) / (1024 * 1024) });
            mdata_p.push({ time: memory.time, used: memory.pageFault / (1024 * 1024) });
            mdata_m.push({ time: memory.time, used: memory.malloc / (1024 * 1024) });
        }
        for (const snapshot of this._model.snapshots) {
            sdata_t.push({ time: snapshot.time, used: (snapshot.pageFault + snapshot.malloc) / (1024 * 1024), name: snapshot.name });
        }
        // @ts-ignore there's probably a nice way to do this
        mdata_t.columns = ["time", "used"];
        // @ts-ignore
        mdata_p.columns = ["time", "used"];
        // @ts-ignore
        mdata_m.columns = ["time", "used"];

        // @ts-ignore
        this._line.x.domain(extent(mdata_t, d => d.time));
        // @ts-ignore
        this._line.y.domain([0, max(mdata_t, d => d.used)]);

        const lineColors = ["steelblue", "red", "green"];

        // @ts-ignore
        this._line.svg.selectAll("lines")
            .data([mdata_t, mdata_m, mdata_p])
            .enter()
            .append("path")
            .attr("fill", "none")
            .attr("stroke", (d: unknown, i: number) => { return lineColors[i]; })
            .attr("stroke-width", 4)
            .attr("d", this._line.valueLine)

        const toolTip = select("body").append("div")
            .attr("class", "tooltip")
            .style("opacity", 0);

        // @ts-ignore
        this._line.svg.selectAll("circles")
            .data(sdata_t)
            .enter()
            .append("circle")
            .attr("fill", "red")
            .attr("stroke", "none")
            // @ts-ignore
            .attr("cx", d => this._line.x(d.time))
            // @ts-ignore
            .attr("cy", d => this._line.y(d.used))
            .attr("r", 3)
            .on("mouseover", function(event: MouseEvent, d: StackData) {
                /* eslint-disable-next-line no-invalid-this */ /* @ts-ignore */
                select(this).transition()
                    .duration(100)
                    .attr("r", 8);
                let str = `${d.used.toFixed(2)}`;
                let yoff = 28;
                if (d.name) {
                    str += `<br/>(${d.name})`;
                    yoff += 20;
                }
                const avent = event;
                setTimeout(() => {
                    toolTip.transition()
                        .duration(200)
                        .style("opacity", 0.9);
                    toolTip.html(str)
                        .style("left", (avent.pageX) + "px")
                        .style("top", (avent.pageY - yoff) + "px");
                }, 100);
            }).on("mouseout", function() {
                /* eslint-disable-next-line no-invalid-this */ /* @ts-ignore */
                select(this).transition()
                    .duration(50)
                    .attr("r", 3);
                toolTip.transition()
                    .duration(500)
                    .style("opacity", 0);
                // @ts-ignore
            }).on("click", (e, d, i) => {
                this._flameify(d.time, e.shiftKey);
                console.log("clk", d, i);
            });

        // @ts-ignore
        this._line.svg.append("g")
            .attr("transform", `translate(0,${this._line.height})`)
            .call(axisBottom(this._line.x));

        // @ts-ignore
        this._line.svg.append("g")
            .call(axisLeft(this._line.y));

        console.log("inited");
    }

    private _flameify(time: number, delta: boolean) {
        const deltaFrom = delta ? this._prevSnapshot : undefined;
        this._prevSnapshot = time;

        const data = this._buildSnapshot(time);
        if (deltaFrom !== undefined) {
            const prev = this._buildSnapshot(deltaFrom);
            // diff data and prev
            Graph._diff(data, prev);
        }

        select("#flamechart")
            .datum(data)
            .call(this._flame);
    }

    private _buildSnapshot(time: number): GraphSnapshot {
        if (this._model === undefined) {
            throw new Error("_buildSnapshot called with no model");
        }

        // find this snapshot
        let snapshot: Snapshot | undefined;
        for (const s of this._model.snapshots) {
            if (s.time === time) {
                snapshot = s;
                break;
            }
        }

        const children: GraphChild[] = [];
        const data: GraphSnapshot = { name: "nrdp", value: 0, children };

        if (snapshot === undefined) {
            throw new Error(`no such snapshot ${time}`);
        }

        // build a size per stack data structure
        const byStack: Map<number, number> = new Map();
        for (const pf of snapshot.pageFaults) {
            byStack.set(pf.stackIdx, (byStack.get(pf.stackIdx) || 0) + PageSize);
        }
        for (const m of snapshot.mallocs) {
            byStack.set(m.stackIdx, (byStack.get(m.stackIdx) || 0) + m.size);
        }

        let cur = children;
        for (const [ stackIdx, bytes ] of byStack) {
            //console.log(stackid, pfs.length);
            const stack = this._model.stacks[stackIdx];
            //console.log(Stack.print(stack, this._data.strings));

            for (let stackNo = stack.length - 1; stackNo >= 0; --stackNo) {
                const stackFrame = stack[stackNo];
                if (!stackFrame) {
                    continue;
                }
                let frame = stackFrame.frame;
                if (!frame) {
                    frame = [-1, -1, 0];
                }
                let curIdx = cur.findIndex(e => {
                    return e.ip === stackFrame.ip;
                });
                if (curIdx === -1) {
                    curIdx = cur.length;
                    cur.push({ name: Stack.stringifyFrame(frame, this._model.stackStrings), value: bytes, ip: stackFrame.ip, children: [] });
                } else {
                    cur[curIdx].value += bytes;
                }
                cur = cur[curIdx].children;
            }
            // console.log(stack, pfsize);

            cur = children;
        }

        return data;
    }

    private static _walkStack(children: GraphChild[], cb: (stack: WalkEntry[]) => boolean) {
        const stack: WalkEntry[] = [];

        const walk = (children: GraphChild[], cb: (stack: WalkEntry[]) => boolean) => {
            for (const c of children) {
                if (c.value < 0) {
                    return true;
                }
                stack.push({ ip: c.ip, child: c });
                if (c.children.length > 0) {
                    if (!walk(c.children, cb)) {
                        return false;
                    }
                } else {
                    if (!cb(stack)) {
                        return false;
                    }
                }
                stack.pop();
            }
            return true;
        };

        walk(children, cb);
    }

    // modifies newSnapshot in-place
    private static _diff(newSnapshot: GraphSnapshot, oldSnapshot: GraphSnapshot) {
        const stackEquals = (a: WalkEntry[], b: WalkEntry[]) => a.length === b.length && a.every((item, index) => b[index].ip === item.ip);

        // build a list of leaf stacks for new and old snapshots
        const newStacks: WalkStack[] = [];
        Graph._walkStack(newSnapshot.children, (entries: WalkEntry[]) => {
            let h = "";
            for (const e of entries) {
                h += e.ip + ":";
            }
            newStacks.push({ hash: cyrb53(h), entries: entries.slice(0) });
            return true;
        });

        const oldStacks: WalkStack[] = [];
        Graph._walkStack(oldSnapshot.children, (entries: WalkEntry[]) => {
            let h = "";
            for (const e of entries) {
                h += e.ip + ":";
            }
            oldStacks.push({ hash: cyrb53(h), entries: entries.slice(0) });
            return true;
        });

        // walk each new leaf node, then find the exact same stack in the other snapshot
        for (const ns of newStacks) {
            // find this stack in oldStacks
            for (const os of oldStacks) {
                if (os.hash === ns.hash && stackEquals(os.entries, ns.entries)) {
                    assert(ns.entries.length === os.entries.length);
                    let removed = 0, done = false;
                    for (let i = ns.entries.length - 1; i >= 0; --i) {
                        const newChild = ns.entries[i].child;
                        assert(newChild !== undefined);
                        if (!done) {
                            const oldChild = os.entries[i].child;
                            assert(oldChild !== undefined);
                            newChild.value -= oldChild.value;
                            if (newChild.value <= 0) {
                                removed = oldChild.value + newChild.value;
                                newChild.value = -1;
                                done = true;
                            }
                            if (removed === 0) {
                                break;
                            }
                        } else {
                            newChild.value -= removed;
                            if (newChild.value === 0) {
                                newChild.value = -1;
                            }
                        }
                    }
                    break;
                }
            }
        }

        // then finally go and remove any children with a value < 0
        const remove = (children: GraphChild[]) => {
            let i = 0;
            while (i < children.length) {
                if (children[i].value >= 0) {
                    remove(children[i].children);
                    ++i;
                } else {
                    // splice it out
                    children.splice(i, 1);
                }
            }
        }

        remove(newSnapshot.children);
    }
}

