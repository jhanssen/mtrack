/* global d3 */

import { Model } from "../../model/Model.js";

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
            .attr("class", "linechart")
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
}
