export type SingleFrame = [number, number, number];
export type FrameWithInlines = [number, number, number, SingleFrame[]];
export type Frame = SingleFrame | FrameWithInlines;
