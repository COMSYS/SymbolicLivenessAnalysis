#include "DebugInfiniteLoopDetection.h"

namespace klee {

llvm::cl::list<DebugInfiniteLoopDetectionType> DebugInfiniteLoopDetection(
    "debug-infinite-loop-detection",
    llvm::cl::desc("Log information about Infinite Loop Detection."),
    llvm::cl::values(
        clEnumValN(STDERR_STATE, "state:stderr",
          "Log all MemoryState information to stderr"),
        clEnumValN(STDERR_TRACE, "trace:stderr",
          "Log all MemoryTrace information to stderr"),
        clEnumValEnd),
    llvm::cl::CommaSeparated);

}
