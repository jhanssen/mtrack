import { Allocation } from "./Allocation";
import { Range } from "./Range";

export class PageFault extends Allocation {
    readonly mmapStack: number | undefined;

    constructor(range: Range, pageFaultStack: number, mmapStack: number | undefined, thread: number, time: number) {
        super(range, pageFaultStack, thread, time);
        this.mmapStack = mmapStack;
    }
}
