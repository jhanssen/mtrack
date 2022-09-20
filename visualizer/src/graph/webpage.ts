import { Graph } from "./Graph";

interface WindowGraphData {
    flamePress?: () => void;
    flameUp?: () => void;
    flameReset?: () => void;
    inputTimer?: ReturnType<typeof setTimeout>;
}

declare global {
    interface Window {
        graphData?: WindowGraphData;
    }
}

window.graphData = {};

const graph = new Graph();
graph.ready().then(() => {
    graph.init();
}).catch(e => {
    console.error("webpage error", e);
});

function flameSearch() {
    const inp = document.getElementById("flameterm") as HTMLInputElement;
    graph.flameSearch(inp.value);
}

graph.onFlameReset = () => {
    if (window.graphData.flameReset) {
        window.graphData.flameReset();
    }
};

window.graphData.flamePress = function() {
    if (window.graphData.inputTimer) {
        clearTimeout(window.graphData.inputTimer);
    }
    window.graphData.inputTimer = setTimeout(flameSearch, 100);
}

window.graphData.flameUp = function() {
    if (window.graphData.inputTimer) {
        clearTimeout(window.graphData.inputTimer);
    }
    window.graphData.inputTimer = setTimeout(flameSearch, 100);
}

window.graphData.flameReset = function() {
    if (window.graphData.inputTimer) {
        clearTimeout(window.graphData.inputTimer);
    }
    const inp = document.getElementById("flameterm") as HTMLInputElement;
    inp.value = "";
    graph.flameSearch(inp.value);
}
