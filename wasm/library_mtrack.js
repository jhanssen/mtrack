var Mtracking = {
    $mtrack__deps: [],
    $mtrack__postset: 'mtrack.init()',
    $mtrack: {
        enabled: false,
        dlsym_writeBytes: undefined,
        dlsym_snapshot: undefined,
        init: () => {
            try {
                mtrack.dlsym_writeBytes = nrdp_platform.dlsym("void mtrack_writeBytes(const Pointer *data, int size)", { library: "RTLD_DEFAULT" });
                mtrack.dlsym_snapshot = nrdp_platform.dlsym("void mtrack_snapshot(String name, int size)", { library: "RTLD_DEFAULT" });
                if(!Module["preRun"])
                    Module["preRun"] = [];
                Module["preRun"].push((module) => {
                    module["onRealloc"] = module["_mtrack_report_realloc"];
                    module["onMalloc"] = module["_mtrack_report_malloc"];
                    module["onFree"] = module["_mtrack_report_free"];
                });
                mtrack.enabled = true;
            } catch(e) {
                console.log(e);
            }
        }
    },
    mtrack_enabled: function() {
        return mtrack.enabled;
    },
    mtrack_stack: function(maxframes, str, maxbytes) {
        const oldStackTraceLimit = Error.stackTraceLimit;
        Error.stackTraceLimit = maxframes;
        const callstack = new Error().stack;
        Error.stackTraceLimit = oldStackTraceLimit;
        if (!str || maxbytes <= 0)
            return lengthBytesUTF8(callstack)+1;
        return stringToUTF8(callstack, str, maxbytes)+1;
    },
    mtrack_writeBytes: function(data, size) {
        mtrack.dlsym_writeBytes(HEAPU8, data, size);
    },
    mtrack_snapshot: function(namePtr) {
        if(mtrack.dlsym_snapshot) {
            const name = UTF8ToString(namePtr);
            console.log(`Snapshot ${name}`);
            mtrack.dlsym_snapshot(name, name.length);
        }
    }
};

autoAddDeps(Mtracking, '$mtrack');
mergeInto(LibraryManager.library, Mtracking);
