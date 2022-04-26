/* global d3, flamegraph */

import { Model } from "../../model/Model.js";
import { Stack } from "../../model/Stack.js";

export class Graph {
    constructor() {
        const margin = {top: 20, right: 20, bottom: 50, left: 70};
        const width = 750 - margin.left - margin.right;
        const height = 400 - margin.top - margin.bottom;

        const x = d3.scaleLinear().range([0, width]);
        const y = d3.scaleLinear().range([height, 0]);

        const valueLine = d3.line()
              .x(d => x(d.time))
              .y(d => y(d.used));

        const svg = d3.select("#linechart")
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
            .transitionEase(d3.easeCubic)
            .sort(true)
            .title("")
            //.onClick(onClick)
            .selfValue(false);

        this._data = "$DATA_GOES_HERE$";
        this._model = new Model(this._data);
    }

    init() {
        if (this._data === "$DATA_GOES_HERE$") {
            window.alert("No data");
        }

        const data = [];
        this._model.parse(undefined, snapshot => {
            data.push({ time: snapshot.time, used: snapshot.used / (1024 * 1024) });
        });
        data.columns = ["time", "used"];

        this._line.x.domain(d3.extent(data, d => d.time));
        this._line.y.domain([0, d3.max(data, d => d.used)]);

        this._line.svg.append("path")
            .data([data])
            .attr("fill", "none")
            .attr("stroke", "steelblue")
            .attr("stroke-width", 4)
            .attr("d", this._line.valueLine);

        this._line.svg.selectAll("circles")
            .data(data)
            .enter()
            .append("circle")
            .attr("fill", "red")
            .attr("stroke", "none")
            .attr("cx", d => this._line.x(d.time))
            .attr("cy", d => this._line.y(d.used))
            .attr("r", 3)
            .on("mouseover", function(d, i) {
                d3.select(this).transition()
                    .duration(100)
                    .attr("r", 8);
            }).on("mouseout", function(d, i) {
                d3.select(this).transition()
                    .duration(50)
                    .attr("r", 3);
            }).on("click", (d, i) => {
                this._flameify(d.time);
                console.log("clk", d, i);
            });

        this._line.svg.append("g")
            .attr("transform", `translate(0,${this._line.height})`)
            .call(d3.axisBottom(this._line.x));

        this._line.svg.append("g")
            .call(d3.axisLeft(this._line.y));

        console.log("inited");
    }

    _flameify(time) {
        const children = [];
        const data = { name: "nrdp", value: 0, children };

        this._model.parse({ ms: time });

        let cur = children;
        for (const [ stackid, pfs ] of this._model.pageFaultsByStack) {
            //console.log(stackid, pfs.length);
            const stack = this._data.stacks[stackid];
            //console.log(Stack.print(stack, this._data.strings));
            let pfsize = 0;
            for (const pf of pfs) {
                pfsize += pf.range.length;
            }

            for (let stackIdx = stack.length - 1; stackIdx >= 0; --stackIdx) {
                const stackEntry = stack[stackIdx];
                const key = `${stackEntry[0]}:${stackEntry[1]}:${stackEntry[2]}`;
                let curIdx = cur.findIndex(e => {
                    return e.key === key;
                });
                if (curIdx === -1) {
                    curIdx = cur.length;
                    cur.push({ name: Stack.stringifyFrame(stackEntry, this._data.strings), value: pfsize, key, children: [] });
                } else {
                    cur[curIdx].value += pfsize;
                }
                cur = cur[curIdx].children;
            }
            // console.log(stack, pfsize);

            cur = children;
        }

        console.log(data);

        d3.select("#flamechart")
            .datum(data)
            .call(this._flame);
    }
}
