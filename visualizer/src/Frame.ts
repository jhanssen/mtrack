export type SingleFrame = [number, number, number];
export type FrameWithInlines = [number, number, number, SingleFrame[]];
export type Frame = SingleFrame | FrameWithInlines;

export function stringifyFrame(frame: Frame, stringTable: string[]): string {
    let str: string;
    if (frame[0] === -1) {
        str = "<unknown>";
    } else {
        str = stringTable[frame[0]];
    }
    if (frame[1] !== -1) {
        str += ` ${stringTable[frame[1]]}`;
        if (frame[2] !== 0) {
            str += `:${frame[2]}`;
        }
    }
    return str;
}
