import { Frame, SingleFrame } from "./Frame";

/*
  Frame: number[] [ function, file, line ],
  Frame with inlines: [ function, file, line, ...rest: Frame[] ]
  stack: Frame[]
*/

export class Stack {
    private frames: string[];

    constructor(stack: Frame[], stringTable: string[]) {
        this.frames = [];
        stack.forEach((frame: Frame) => {
            // console.log("got dude", frame);
            this.frames.push(Stack.stringifyFrame(frame, stringTable));
            if (Array.isArray(frame[3])) {
                frame[3].forEach((inline: SingleFrame) => {
                    this.frames.push(Stack.stringifyFrame(inline, stringTable));
                });
            }
        });
    }

    toString(): string {
        return this.frames.join("\n");
    }

    static stringifyFrame(frame: Frame, stringTable: string[]): string {
        let str;
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

    static print(stack: Frame[], stringTable: string[]): string {
        return new Stack(stack, stringTable).toString();
    }
}



