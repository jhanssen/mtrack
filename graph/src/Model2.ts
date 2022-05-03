import { Frame, SingleFrame } from "./Frame";
import { PageFault2 } from "./PageFault2";
import { ParseResult } from "./ParseResult";
import { Snapshot } from "./Snapshot";
import { Until } from "./Until";

// needs to match EmitType in Parser.cpp
const enum EventType {
    Stack,
    StackString,
    StackAddr,
    StackIp,
    Malloc,
    Mmap,
    PageFault,
    ThreadName,
    Time
}

type FrameOrSingleFrame = Frame | SingleFrame | undefined;
type Stack = FrameOrSingleFrame[];

export class Model2 {
    private _decoder: TextDecoder;
    private _data: ArrayBuffer;
    private _view: DataView;
    private _offset: number;
    private _pageFaultMap?: Map<number, PageFault2[]>;
    private _stacks: Stack[] | undefined;
    private _stackStrings: string[] | undefined;

    constructor(data: ArrayBuffer) {
        this._decoder = new TextDecoder;
        this._data = data;
        this._view = new DataView(this._data);
        this._offset = 0;
    }

    private _readFloat64() {
        const n = this._view.getFloat64(this._offset);
        this._offset += 8;
        return n;
    }

    private _readUint32() {
        const n = this._view.getUint32(this._offset);
        this._offset += 4;
        return n;
    }

    private _readInt32() {
        const n = this._view.getInt32(this._offset);
        this._offset += 4;
        return n;
    }

    private _readString() {
        const n = this._readUint32();
        const str = this._decoder.decode(new Uint8Array(this._data, this._offset, n));
        this._offset += n;
        return str;
    }

    public parse(until?: Until, callback?: (snapshot: Snapshot) => void): ParseResult {
        let eventNo = 0;
        let pageFaults = 0;
        let mallocs = 0;
        let pageFaultSize = 0;
        let mallocSize = 0;
        let time = 0;
        let callbackAt: number | undefined;
        const stacks: FrameOrSingleFrame[][] = [];
        const stackStrings: string[] = [];
        const stackAddrs: Map<number, FrameOrSingleFrame> = new Map();
        const threads: Map<number, string> = new Map();
        let stackIdx = 0;

        this._pageFaultMap = new Map();

        const sendCallback = () => {
            if (!callback) {
                return;
            }
            callback(new Snapshot(pageFaultSize, time, eventNo));
        };

        while (this._offset < this._data.byteLength) {
            const et = this._view.getUint8(this._offset++);
            switch (et) {
            case EventType.Stack: {
                const idx = this._readUint32();
                if (idx >= stacks.length) {
                    stacks[idx] = [];
                }
                stackIdx = idx;
                break; }
            case EventType.StackString: {
                const str = this._readString();
                stackStrings.push(str);
                break; }
            case EventType.StackAddr: {
                const ip = this._readFloat64();
                const func = this._readInt32();
                const file = this._readInt32();
                const line = this._readInt32();
                const inlsz = this._readInt32();
                let frame: FrameOrSingleFrame;
                if (inlsz === 0) {
                    frame = [func, file, line];
                } else {
                    const inls: SingleFrame[] = []
                    for (let inl = 0; inl < inlsz; ++inl) {
                        const func = this._readInt32();
                        const file = this._readInt32();
                        const line = this._readInt32();
                        inls.push([func, file, line]);
                    }
                    frame = [func, file, line, inls];
                }
                stackAddrs.set(ip, frame);
                break; }
            case EventType.StackIp: {
                const ip = this._readFloat64();
                // goes into stackIdx
                const addr = stackAddrs.get(ip);
                if (addr === undefined) {
                    // should not happen
                    throw new Error(`Got undefined for stack ip ${ip}`);
                }
                stacks[stackIdx].push(addr);
                break; }
            case EventType.Malloc: {
                ++mallocs;
                const ptid = this._readUint32();
                const sz = this._readFloat64();
                mallocSize = sz;
                break; }
            case EventType.Mmap: {
                const addr = this._readFloat64();
                const size = this._readFloat64();
                break; }
            case EventType.PageFault: {
                ++pageFaults;
                const addr = this._readFloat64();
                const ptid = this._readUint32();
                const sz = this._readFloat64();
                pageFaultSize = sz;

                const pageFaultEntry = this._pageFaultMap.get(stackIdx);
                const pf2 = new PageFault2(stackIdx, undefined, ptid, time);
                if (!pageFaultEntry) {
                    this._pageFaultMap.set(stackIdx, [ pf2 ]);
                } else {
                    pageFaultEntry.push(pf2);
                }
                break; }
            case EventType.ThreadName: {
                const ptid = this._readUint32();
                const fn = this._readString();
                threads.set(ptid, fn);
                break; }
            case EventType.Time: {
                const ts = this._readUint32();
                if (ts > time) {
                    // call callback
                    callbackAt = eventNo;
                    sendCallback();
                }
                time = ts;
                if (until && until.ms as number >= time) {
                    break;
                }
                break; }
            default:
                throw new Error(`Unhandled event type: ${et}`);
            }
            if (until && until.event === eventNo) {
                break;
            }
            ++eventNo;
        }

        if (callbackAt === undefined || callbackAt < eventNo - 1) {
            sendCallback();
        }

        this._stacks = stacks;
        this._stackStrings = stackStrings;

        return { events: eventNo, pageFaults, time };
    }

    get pageFaultsByStack(): Map<number, PageFault2[]> {
        if (!this._pageFaultMap) {
            throw new Error("Not parsed");
        }
        return this._pageFaultMap;
    }

    get stacks(): Stack[] {
        if (!this._stacks) {
            throw new Error("Not parsed");
        }
        return this._stacks;
    }

    get stackStrings(): string[] {
        if (!this._stackStrings) {
            throw new Error("Not parsed");
        }
        return this._stackStrings;
    }
}
