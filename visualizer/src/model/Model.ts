import { Frame, SingleFrame } from "./Frame";
import { assert } from "../Assert";

export const PageSize = 4096;

// needs to match EmitType in Parser.cpp
const enum EventType {
    Memory,
    Snapshot,
    SnapshotName,
    Stack,
    StackAddr,
    StackString,
    ThreadName
}

type FrameOrSingleFrame = Frame | SingleFrame;

interface StackFrame {
    ip: number,
    frame?: FrameOrSingleFrame
}

type Stack = StackFrame[];

export interface Memory {
    time: number;
    pageFault: number;
    malloc: number;
}

interface Pagefault {
    place: number;
    ptid: number;
    stackIdx: number;
    time: number;
}

interface Malloc {
    addr: number;
    size: number;
    ptid: number;
    stackIdx: number;
    time: number;
}

interface Mmap {
    start: number;
    end: number;
    stackIdx: number;
}

export interface Snapshot {
    name?: string;
    time: number;
    pageFault: number;
    malloc: number;

    pageFaults: Pagefault[];
    mallocs: Malloc[];
    mmaps: Mmap[];
}

export const enum CallbackType {
    Memory,
    Snapshot
}

export class Model {
    private _decoder: TextDecoder;
    private _data: ArrayBuffer;
    private _view: DataView;
    private _offset: number;
    private _stackStrings: string[] | undefined;
    private _stacks: Stack[] | undefined;
    private _memories: Memory[] | undefined;
    private _snapshots: Snapshot[] | undefined;
    private _parsed: boolean;

    constructor(data: ArrayBuffer) {
        this._decoder = new TextDecoder;
        this._data = data;
        this._view = new DataView(this._data);
        this._offset = 0;
        this._parsed = false;
    }

    private _readFloat64() {
        const n = this._view.getFloat64(this._offset, true);
        this._offset += 8;
        return n;
    }

    private _readUint32() {
        const n = this._view.getUint32(this._offset, true);
        this._offset += 4;
        return n;
    }

    private _readInt32() {
        const n = this._view.getInt32(this._offset, true);
        this._offset += 4;
        return n;
    }

    private _readString() {
        const n = this._readUint32();
        const str = this._decoder.decode(new Uint8Array(this._data, this._offset, n));
        this._offset += n;
        return str;
    }

    public parse() {
        if (this._parsed) {
            return;
        }
        this._parsed = true;
        const stacks: Stack[] = [];
        const stackStrings: string[] = [];
        const threads: Map<number, string> = new Map();
        const ipToStacks: Map<number, Stack[]> = new Map();
        const ipToFrame: Map<number, FrameOrSingleFrame | undefined> = new Map();
        const memories: Memory[] = [];
        const snapshots: Snapshot[] = [];

        while (this._offset < this._data.byteLength) {
            const et = this._view.getUint8(this._offset++);
            // console.log("got event", et);
            switch (et) {
            case EventType.Stack: {
                const idx = this._readInt32();
                if (idx >= stacks.length || stacks[idx] === undefined) {
                    stacks[idx] = [];
                }
                const numFrames = this._readUint32();
                for (let n = 0; n < numFrames; ++n) {
                    const ip = this._readFloat64();
                    const frame = ipToFrame.get(ip);
                    stacks[idx].push({ ip, frame });
                    if (frame === undefined) {
                        const ipts = ipToStacks.get(ip);
                        if (ipts === undefined) {
                            ipToStacks.set(ip, [ stacks[idx] ]);
                        } else {
                            ipts.push(stacks[idx]);
                        }
                    }
                }
                break; }
            case EventType.StackString: {
                const idx = this._readInt32();
                const str = this._readString();
                stackStrings[idx] = str;
                break; }
            case EventType.StackAddr: {
                let frame: FrameOrSingleFrame | undefined;
                const ip = this._readFloat64();
                const numf = this._readUint32();
                for (let n = 0; n < numf; ++n) {
                    const func = this._readInt32();
                    const file = this._readInt32();
                    const line = this._readInt32();
                    if (n === 0) {
                        frame = [func, file, line];
                    } else {
                        assert(frame !== undefined);
                        if (frame[3] === undefined) {
                            frame[3] = [ [func, file, line] ];
                        } else {
                            frame[3].push([func, file, line]);
                        }
                    }
                }
                ipToFrame.set(ip, frame);
                if (frame !== undefined) {
                    const ipts = ipToStacks.get(ip);
                    if (ipts !== undefined) {
                        for (const st of ipts) {
                            for (let n = 0; n < st.length; ++n) {
                                if (st[n].ip === ip) {
                                    st[n].frame = frame;
                                }
                            }
                        }
                        ipToStacks.delete(ip);
                    }
                }
                break; }
            case EventType.Memory: {
                const time = this._readUint32();
                const pageFault = this._readFloat64();
                const malloc = this._readFloat64();
                memories.push({ time, pageFault, malloc });
                break; }
            case EventType.Snapshot: {
                const time = this._readUint32();
                const pageFault = this._readFloat64();
                const malloc = this._readFloat64();
                memories.push({ time, pageFault, malloc });
                const numPfs = this._readUint32();
                const numMallocs = this._readUint32();
                const numMmaps = this._readUint32();
                const snapshot: Snapshot = { time, pageFault, malloc, pageFaults: [], mallocs: [], mmaps: [] };
                for (let n = 0; n < numPfs; ++n) {
                    const place = this._readFloat64();
                    const ptid = this._readUint32();
                    const stackIdx = this._readInt32();
                    const time = this._readUint32();
                    snapshot.pageFaults.push({ place, ptid, stackIdx, time });
                }
                for (let n = 0; n < numMallocs; ++n) {
                    const addr = this._readFloat64();
                    const size = this._readFloat64();
                    const ptid = this._readUint32();
                    const stackIdx = this._readInt32();
                    const time = this._readUint32();
                    snapshot.mallocs.push({ addr, size, ptid, stackIdx, time });
                }
                for (let n = 0; n < numMmaps; ++n) {
                    const start = this._readFloat64();
                    const end = this._readFloat64();
                    const stackIdx = this._readInt32();
                    snapshot.mmaps.push({ start, end, stackIdx });
                }
                snapshots.push(snapshot);
                break; }
            case EventType.SnapshotName: {
                const n = this._readString();
                snapshots[snapshots.length - 1].name = n ? n : undefined;
                break; }
            case EventType.ThreadName: {
                const ptid = this._readUint32();
                const fn = this._readString();
                threads.set(ptid, fn);
                break; }
            default:
                throw new Error(`Unhandled event type: ${et}`);
            }
        }

        this._snapshots = snapshots;
        this._memories = memories.sort((m1, m2) => {
            return m1.time - m2.time;
        });
        // console.log(this._memories);
        this._stacks = stacks;
        this._stackStrings = stackStrings;
    }

    get stackStrings(): string[] {
        if (!this._stackStrings) {
            throw new Error("Not parsed");
        }
        return this._stackStrings;
    }

    get stacks(): Stack[] {
        if (!this._stacks) {
            throw new Error("Not parsed");
        }
        return this._stacks;
    }

    get memories(): Memory[] {
        if (!this._memories) {
            throw new Error("Not parsed");
        }
        return this._memories;
    }

    get snapshots(): Snapshot[] {
        if (!this._snapshots) {
            throw new Error("Not parsed");
        }
        return this._snapshots;
    }
}
