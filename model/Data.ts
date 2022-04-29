import { Frame } from "./Frame";

export interface Data {
    events: Array<number[]>;
    stacks: Array<Frame[]>;
    strings: string[];
}

/*
{
   "events" : [
      [15, 0],
       [15, 252],
       [9, 0, 65536, 0, 2, 1, 12, 0, 4294967295, 0],
       [8, 0, 100663296, 100663296, 0, 34, -1, 0, 4294967295, 1]
   ],
   "stacks" : [
      [
         [2078, 137, 1086],
         [200, 139, 297],
         [141, 100, 636],
         [142, 143, 473],
         [144, -1, 0]
      ]
   ],
   "strings" : [
      "/home/abakken/dev/builds/nrdp-22.1-debug-64/src/platform/gibbon/netflix",
      "/home/abakken/dev/builds/mtrack64/preload/libmtrack_preload.so",
      "/lib/x86_64-linux-gnu/libdl.so.2",
      "/lib/x86_64-linux-gnu/librt.so.1"
   ]
}
*/

