import { Graph } from "./Graph";

const graph = new Graph();
graph.ready().then(() => {
    graph.init();
}).catch(e => {
    console.error("webpage error", e);
});
