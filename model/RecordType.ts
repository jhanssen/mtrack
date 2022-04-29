export const enum RecordType {
    Invalid = 0,
    Executable = 1,
    Free = 2,
    Library = 3,
    LibraryHeader = 4,
    MadviseTracked = 5,
    MadviseUntracked = 6,
    Malloc = 7,
    MmapTracked = 8,
    MmapUntracked = 9,
    MunmapTracked = 10,
    MunmapUntracked = 11,
    PageFault = 12,
    ThreadName = 13,
    Time = 14,
    WorkingDirectory = 15
}
