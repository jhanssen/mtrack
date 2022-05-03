import { Graph } from "./Graph-template";

const graph = new Graph();
graph.ready().then(() => {
    graph.init();
}).catch(e => {
    console.error("webpage error", e);
});
