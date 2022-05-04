export function assert(value: unknown): asserts value {
    if (value === false || value === undefined) {
        throw new Error(`assert: ${value}`);
    }
}
