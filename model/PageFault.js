class PageFault
{
    constructor(range, pageFaultStack, mmapStack, thread, time)
    {
        this.range = range;
        this.pageFaultStack = pageFaultStack;
        this.mmapStack = mmapStack;
        this.time = time;
    }

    toString()
    {
        return `PageFault(range: ${this.range}, pageFaultStack: ${this.pageFaultStack}, mmapStack: ${this.mmapStack}, thread: ${this.thread}, time: ${this.time})`;
    }
};

module.exports = PageFault;
