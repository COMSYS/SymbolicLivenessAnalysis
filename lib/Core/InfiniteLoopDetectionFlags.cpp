#include "InfiniteLoopDetectionFlags.h"

namespace klee {

llvm::cl::opt<bool> DetectInfiniteLoops(
  "detect-infinite-loops",
  llvm::cl::desc("Enable detection of infinite loops (default=false)"),
  llvm::cl::init(false));

llvm::cl::bits<DebugInfiniteLoopDetectionType> DebugInfiniteLoopDetection(
  "debug-infinite-loop-detection",
  llvm::cl::desc("Log information about Infinite Loop Detection."),
  llvm::cl::values(
    clEnumValN(STDERR_STATE, "state:stderr",
      "Log all MemoryState information to stderr"),
    clEnumValN(STDERR_TRACE, "trace:stderr",
      "Log all MemoryTrace information to stderr"),
        clEnumValEnd),
  llvm::cl::CommaSeparated);

llvm::cl::opt<bool> InfiniteLoopDetectionTruncateOnFork(
  "infinite-loop-detection-truncate-on-fork",
  llvm::cl::desc("Truncate memory trace (used for infinite loop detection) on every state fork (default=false)"),
  llvm::cl::init(false));

}
