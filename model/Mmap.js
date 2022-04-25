class Mmap
{
    constructor(range, stack, thread, time)
    {
        this.range = range;
        this.stack = stack;
        this.thread = thread;
        this.time = time;
    }

    clone(range)
    {
        return new Mmap(range || this.range.clone(), this.stack, this.thread, this.time);
    }

    toString()
    {
        return `Mmap(range: {this.range}, stack: ${this.stack}, thread: ${this.thread}, time: ${this.time})`;
    }
};

export { Mmap };
