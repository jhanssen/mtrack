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
              .x(d => x(d.date))
              .y(d => y(d.close));

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
            data.push({ date: snapshot.time, close: snapshot.used / (1024 * 1024) });
        });
        data.columns = ["date", "close"];

        this._line.x.domain(d3.extent(data, d => d.date));
        this._line.y.domain([0, d3.max(data, d => d.close)]);

        this._line.svg.append("path")
            .data([data])
            .attr("class", "linechart")
            .attr("d", this._line.valueLine);

        this._line.svg.append("g")
            .attr("transform", `translate(0,${this._line.height})`)
            .call(d3.axisBottom(this._line.x));

        this._line.svg.append("g")
            .call(d3.axisLeft(this._line.y));

        console.log("inited");
    }
}
