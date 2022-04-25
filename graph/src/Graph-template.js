/* global d3 */

import { Model } from "../../model/Model.js";

export class Graph {
    constructor() {
        const colors = d3.ez.palette.categorical(3);
        const chart = d3.ez.chart.lineChart()
              .colors(colors)
              .yAxisLabel("Rate");

        const legend = d3.ez.component.legend().title("Currency");
        const title = d3.ez.component.title().mainText("Historical Exchange Rates").subText("Comparison against GBP");

        this.linechart = d3.ez.base()
            .width(750)
            .height(400)
            .chart(chart)
            .legend(legend)
            .title(title)
            .on("customValueMouseOver", function(d, i) {
                d3.select("#linemessage").text(d.value);
            });

        this._data = "$DATA_GOES_HERE$";
    }

    init() {
        if (this._data === "$DATA_GOES_HERE$") {
            window.alert("No data");
        }

        function nnow(offset) {
            return new Date(new Date().getTime() + (offset * 24 * 60 * 60 * 1000));
        }

        const data = [ {key: "USD", values: []}, {key: "EUR", values: []}, {key: "AUD", values: []} ];
        data[0].values.push({ key: nnow(-5), value: 5 });
        data[0].values.push({ key: nnow(-4), value: 10 });
        data[0].values.push({ key: nnow(-3), value: 7 });
        data[0].values.push({ key: nnow(-2), value: 3 });

        data[1].values.push({ key: nnow(-5), value: 50 });
        data[1].values.push({ key: nnow(-4), value: 20 });
        data[1].values.push({ key: nnow(-3), value: 30 });
        data[1].values.push({ key: nnow(-2), value: 30 });

        data[2].values.push({ key: nnow(-5), value: 500 });
        data[2].values.push({ key: nnow(-4), value: 100 });
        data[2].values.push({ key: nnow(-3), value: 30 });
        data[2].values.push({ key: nnow(-2), value: 70 });

        d3.select('#linechart')
            .datum(data)
            .call(this.linechart);

        console.log("inited");
    }
}
