import { Range } from "./Range";

export class Allocation {
    readonly range: Range;
    readonly stack: number;
    readonly thread: number;
    readonly time: number;

    constructor(range: Range, stack: number, thread: number, time: number) {
        this.range = range;
        this.stack = stack;
        this.thread = thread;
        this.time = time;
    }

    toString(): string {
        return `${this.constructor.name}(range: {this.range}, stack: ${this.stack}, thread: ${this.thread}, time: ${this.time})`;
    }
}

