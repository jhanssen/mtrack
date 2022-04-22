class Model
{
    constructor(data)
    {
        this.data = data;
        this.allocationsByInstructionPointer = new Map();
    }

    process(until)
    {
        let count = this.data.events.length;
        if (until.event) {
            count = Math.min(until.event, count);
        }
        for (let idx=0; idx<count; ++idx) {
            console.log("Got event", this.data.events[idx]);
        }
        // console.log(this.strings);
        // console.log(this.stacks);
        console.log(until, Object.keys(this.data));
    }
}


module.exports = Model;
