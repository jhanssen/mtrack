/*
  Frame: [ function: number, file: number, line: number ],
  Frame with inlines: [ function: number, file: number, line: number, ...rest: Frame[] ]
  stack: Frame[]
*/

function stringifyFrame(frame, stringTable)
{
    let str;
    if (frame.function === -1) {
        str = "<unknown>";
    } else {
        str = stringTable[frame.function];
    }
    if (frame.file !== -1) {
        str += ` ${stringTable[frame.file]}`;
        if (frame.line !== 0) {
            str += `:${frame.line}`;
        }
    }
    return str;
}

class Stack
{
    constructor(stack, stringTable)
    {
        this.frames = [];
        stack.forEach(frame => {
            this.frames.push(stringifyFrame(frame, stringTable));
            if (frame.inlines) {
                frame.inlines.forEach(inline => {
                    this.frames.push(stringifyFrame(inline));
                });
            }
        });
    }

    toString()
    {
        return this.frames.join("\n");
    }
}

Stack.print = function(stack, stringTable)
{
    return new Stack(stack, stringTable).toString();
}

module.exports = Stack;
