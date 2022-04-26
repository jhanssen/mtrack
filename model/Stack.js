/*
  Frame: number[] [ function, file, line ],
  Frame with inlines: [ function, file, line, ...rest: Frame[] ]
  stack: Frame[]
*/

export class Stack
{
    constructor(stack, stringTable)
    {
        this.frames = [];
        stack.forEach(frame => {
            // console.log("got dude", frame);
            this.frames.push(Stack.stringifyFrame(frame, stringTable));
            if (frame.inlines) {
                frame.inlines.forEach(inline => {
                    this.frames.push(Stack.stringifyFrame(inline));
                });
            }
        });
    }

    toString()
    {
        return this.frames.join("\n");
    }
}

Stack.stringifyFrame = function(frame, stringTable)
{
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
};

Stack.print = function(stack, stringTable)
{
    return new Stack(stack, stringTable).toString();
};

