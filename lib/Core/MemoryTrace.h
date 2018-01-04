#ifndef KLEE_MEMORYTRACE_H
#define KLEE_MEMORYTRACE_H

#include "klee/Internal/Module/KInstruction.h"

#include <vector>

namespace klee {
class ExecutionState;
struct KFunction;
class MemoryObject;
struct StackFrame;

class MemoryTrace {

using fingerprint_t = MemoryFingerprint::fingerprint_t;

private:
  struct MemoryTraceEntry {
    const KInstruction *inst;
    fingerprint_t fingerprint;

    MemoryTraceEntry(const KInstruction *inst,
                     fingerprint_t fingerprint)
        : inst(inst), fingerprint(fingerprint) {}

    bool operator==(const MemoryTraceEntry &rhs) {
      // check KInstruction first (short-circuit evaluation)
      return (inst == rhs.inst && fingerprint == rhs.fingerprint);
    }

    bool operator!=(const MemoryTraceEntry &rhs) { return !(operator==(rhs)); }
  };

public:
  struct StackFrameEntry {
    // first index in stack that belongs to next stack frame
    std::size_t index;
    // function that is executed in this stack frame
    const KFunction *kf;
    // locals and arguments only visible within this stack frame
    fingerprint_t fingerprintLocalDelta;
    // allocas allocated in this stack frame
    fingerprint_t fingerprintAllocaDelta;
    // did this stack frame contain any global allocation?
    bool globalAllocation;

    StackFrameEntry(std::size_t index,
                    const KFunction *kf,
                    fingerprint_t fingerprintLocalDelta,
                    fingerprint_t fingerprintAllocaDelta,
                    bool globalAllocation)
        : index(index),
          kf(kf),
          fingerprintLocalDelta(fingerprintLocalDelta),
          fingerprintAllocaDelta(fingerprintAllocaDelta),
          globalAllocation(globalAllocation) {}
  };

private:
  std::vector<MemoryTraceEntry> trace;
  std::vector<StackFrameEntry> stackFrames;

public:
  MemoryTrace() = default;
  MemoryTrace(const MemoryTrace &) = default;

  size_t getTraceLength() const {
    return trace.size();
  }
  size_t getStackFramesLength() const {
    return stackFrames.size();
  }

  void registerBasicBlock(const KInstruction *instruction,
                          const fingerprint_t &fingerprint);
  void registerEndOfStackFrame(const KFunction *kf,
                               fingerprint_t fingerprintLocalDelta,
                               fingerprint_t fingerprintAllocaDelta,
                               bool globalAllocation);
  StackFrameEntry popFrame();
  bool findLoop();
  void clear();
  std::size_t getNumberOfStackFrames();

  static bool isAllocaAllocationInCurrentStackFrame(const ExecutionState &state,
                                                    const MemoryObject &mo);
  fingerprint_t *getPreviousAllocaDelta(const ExecutionState &state,
                                        const MemoryObject &mo);

  void dumpTrace(llvm::raw_ostream &out = llvm::errs()) const;
};
}

#endif
