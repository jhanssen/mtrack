export class Malloc
{
    constructor(range, thread, stack, time)
    {
        this.range = range;
        this.stack = stack;
        this.thread = thread;
        this.time = time;
    }

    toString()
    {
        return `Malloc(range: {this.range}, stack: ${this.stack}, thread: ${this.thread}, time: ${this.time})`;
    }
}
