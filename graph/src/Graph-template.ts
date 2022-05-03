import { Data } from "./Data";
import { FlameGraph, flamegraph } from "d3-flame-graph";
import { Line, ScaleLinear, axisBottom, axisLeft, extent, line, max, scaleLinear, select } from "d3";
import { Model2 } from "./Model2";
import { Stack } from "./Stack";

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
    x: ScaleLinear<number, number, never>;
    y: ScaleLinear<number, number, never>;
    svg: unknown;
    valueLine: Line<[number, number]>;
};

type Ready = {
    resolve: () => void | PromiseLike<void>;
    reject: (reason?: any) => void;
};

export class Graph {
    private _line: LineData;
    private _flame: FlameGraph;
    private _model: Model2 | undefined;
    private _nomodel: unknown | undefined;
    private _readies: Ready[] = [];

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
            // ### Had to change this
            .transitionEase("easeCubic")
            .sort(true)
            .title("")
            //.onClick(onClick)
            .selfValue(false);

        const data = "data:application/octet-binary;base64,$DATA_GOES_HERE$";
        window.fetch(data)
            .then(res => res.arrayBuffer())
            .then(buffer => {
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
        const data: { time: number, used: number }[] = [];
        this._model.parse(undefined, snapshot => {
            data.push({ time: snapshot.time, used: snapshot.used / (1024 * 1024) });
        });
        // @ts-ignore there's probably a nice way to do this
        data.columns = ["time", "used"];

        // @ts-ignore
        this._line.x.domain(extent(data, d => d.time));
        // @ts-ignore
        this._line.y.domain([0, max(data, d => d.used)]);

        // @ts-ignore
        this._line.svg.append("path")
            .data([data])
            .attr("fill", "none")
            .attr("stroke", "steelblue")
            .attr("stroke-width", 4)
            .attr("d", this._line.valueLine);

        // @ts-ignore
        this._line.svg.selectAll("circles")
            .data(data)
            .enter()
            .append("circle")
            .attr("fill", "red")
            .attr("stroke", "none")
            // @ts-ignore
            .attr("cx", d => this._line.x(d.time))
            // @ts-ignore
            .attr("cy", d => this._line.y(d.used))
            .attr("r", 3)
            .on("mouseover", function() {
                /* eslint-disable-next-line no-invalid-this */ /* @ts-ignore */
                select(this).transition()
                    .duration(100)
                    .attr("r", 8);
            }).on("mouseout", function() {
                /* eslint-disable-next-line no-invalid-this */ /* @ts-ignore */
                select(this).transition()
                    .duration(50)
                    .attr("r", 3);
                // @ts-ignore
            }).on("click", (d, i) => {
                this._flameify(d.time);
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

    _flameify(time: number) {
        if (this._model === undefined) {
            throw new Error("_flameify called with no model");
        }
        type Child = { name: string, value: number, key: string, children: Child[] };
        const children: Child[] = [];
        const data = { name: "nrdp", value: 0, children };

        this._model.parse({ ms: time });

        let cur = children;
        for (const [ stackid, pfs ] of this._model.pageFaultsByStack) {
            //console.log(stackid, pfs.length);
            const stack = this._model.stacks[stackid];
            //console.log(Stack.print(stack, this._data.strings));
            let pfsize = pfs.length * 4096;

            for (let stackIdx = stack.length - 1; stackIdx >= 0; --stackIdx) {
                const stackEntry = stack[stackIdx];
                if (!stackEntry) {
                    continue;
                }
                const key = `${stackEntry[0]}:${stackEntry[1]}:${stackEntry[2]}`;
                let curIdx = cur.findIndex(e => {
                    return e.key === key;
                });
                if (curIdx === -1) {
                    curIdx = cur.length;
                    cur.push({ name: Stack.stringifyFrame(stackEntry, this._model.stackStrings), value: pfsize, key, children: [] });
                } else {
                    cur[curIdx].value += pfsize;
                }
                cur = cur[curIdx].children;
            }
            // console.log(stack, pfsize);

            cur = children;
        }

        console.log(data);

        select("#flamechart")
            .datum(data)
            .call(this._flame);
    }
}

