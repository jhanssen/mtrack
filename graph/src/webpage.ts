import { Graph } from "./Graph-template";

const graph = new Graph();
graph.ready().then(() => {
    graph.init();
});
