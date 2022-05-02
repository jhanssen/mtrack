import commonjs from "@rollup/plugin-commonjs";
import resolve from "@rollup/plugin-node-resolve";
import typescript from "rollup-plugin-typescript2";

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
        input: "src/cli.ts",
        plugins,
        external,
        output: {
            file: "cli.cjs",
            format,
            name: "cli",
            exports: "named",
            sourcemap: true
        }
    },
    {
        input: "src/webpage.ts",
        plugins,
        external,
        output: {
            file: "index.js",
            format,
            name: "webpage",
            exports: "named",
            sourcemap: true
        }
    }
];