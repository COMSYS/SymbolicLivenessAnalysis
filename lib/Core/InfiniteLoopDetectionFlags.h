#ifndef KLEE_INFINITELOOPDETECTIONFLAGS_H
#define KLEE_INFINITELOOPDETECTIONFLAGS_H

#include "klee/CommandLine.h"

namespace klee {

extern llvm::cl::opt<bool> DetectInfiniteLoops;

enum DebugInfiniteLoopDetectionType {
  STDERR_STATE,
  STDERR_TRACE
};

extern llvm::cl::bits<DebugInfiniteLoopDetectionType> DebugInfiniteLoopDetection;

extern llvm::cl::opt<bool> InfiniteLoopDetectionTruncateOnFork;

}
#endif
