import commonjs from "@rollup/plugin-commonjs";
import resolve from "@rollup/plugin-node-resolve";
import typescript from "rollup-plugin-typescript2";

const output = "mtrack.cjs";
const input = "index.ts";

const plugins = [
    resolve({
        preferBuiltins: true
    }),
    commonjs(),
    typescript({
        tsconfig: `tsconfig.json`,
        cacheRoot: ".cache"
    })
];

// Define forms
const format = "cjs";
const external = ["fs", "assert"];

export default [
    {
        input,
        plugins,
        external,
        output: {
            file: output,
            format,
            name: "mtrack",
            exports: "named",
            sourcemap: true
        }
    }
];
