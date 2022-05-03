export class PageFault2 {
    readonly stack: number;
    readonly mmapStack: number | undefined;
    readonly thread: number;
    readonly time: number;

    constructor(pageFaultStack: number, mmapStack: number | undefined, thread: number, time: number) {
        this.stack = pageFaultStack;
        this.mmapStack = mmapStack;
        this.thread = thread;
        this.time = time;
    }
}
