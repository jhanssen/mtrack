import { Allocation } from "./Allocation";
import { Range } from "./Range";

export class Malloc extends Allocation {
    constructor(range: Range, thread: number, stack: number, time: number) {
        super(range, thread, stack, time);
    }
}


