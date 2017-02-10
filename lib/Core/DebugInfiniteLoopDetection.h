#ifndef KLEE_DEBUGINFINITELOOPDETECTION_H
#define KLEE_DEBUGINFINITELOOPDETECTION_H

#include "klee/CommandLine.h"

namespace klee {

enum DebugInfiniteLoopDetectionType {
  STDERR_STATE,
  STDERR_TRACE
};

extern llvm::cl::list<DebugInfiniteLoopDetectionType> DebugInfiniteLoopDetection;

}
#endif
