import { Allocation } from "./Allocation";
import { Range } from "./Range";

export class Mmap extends Allocation {
    constructor(range: Range, stack: number, thread: number, time: number) {
        super(range, stack, thread, time)
    }

    clone(range?: Range): Mmap {
        return new Mmap(range || this.range.clone(), this.stack, this.thread, this.time);
    }
}


