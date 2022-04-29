export type SingleFrame = [number, number, number];
export type FrameWithInlines = [number, number, number, FrameData[]];
export type Frame = SingleFrame | FrameWithInlines;
