export class Snapshot {
    readonly used: number;
    readonly time: number;
    readonly eventIndex: number;

    constructor(used: number, time: number, eventIndex: number) {
        this.used = used;
        this.time = time;
        this.eventIndex = eventIndex;
    }
}
