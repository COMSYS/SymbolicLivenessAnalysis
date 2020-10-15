#ifndef KLEE_MEMORYTRACE_H
#define KLEE_MEMORYTRACE_H

#include "MemoryFingerprint.h"

#include "klee/Module/KInstruction.h"

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

    bool operator==(const MemoryTraceEntry &rhs) const {
      // check KInstruction first (short-circuit evaluation)
      return (inst == rhs.inst && fingerprint == rhs.fingerprint);
    }

    bool operator!=(const MemoryTraceEntry &rhs) const {
      return !(operator==(rhs));
    }
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

    StackFrameEntry(std::size_t index,
                    const KFunction *kf,
                    fingerprint_t fingerprintLocalDelta,
                    fingerprint_t fingerprintAllocaDelta)
        : index(index),
          kf(kf),
          fingerprintLocalDelta(fingerprintLocalDelta),
          fingerprintAllocaDelta(fingerprintAllocaDelta) {}
  };

private:
  std::vector<MemoryTraceEntry> trace;
  std::vector<StackFrameEntry> stackFrames;

public:
  MemoryTrace() = default;
  MemoryTrace(const MemoryTrace &) = default;

  std::pair<std::size_t, std::size_t> getTraceLength() const {
    return std::make_pair(trace.size(), stackFrames.size());
  }

  std::pair<std::size_t, std::size_t> getTraceCapacity() const {
    return std::make_pair(trace.capacity(), stackFrames.capacity());
  }

  static std::pair<std::size_t, std::size_t> getTraceStructSizes() {
    return std::make_pair(sizeof(MemoryTraceEntry), sizeof(StackFrameEntry));
  }

  std::size_t getNumberOfEntriesInCurrentStackFrame() const {
    auto stackFramesIt = stackFrames.rbegin();
    std::size_t topStackFrameBoundary = 0;
    if (stackFramesIt != stackFrames.rend()) {
      // first index that belongs to current stack frame
      topStackFrameBoundary = stackFramesIt->index;
    }

    // calculate number of entries within first stack frame
    return trace.size() - topStackFrameBoundary;
  }

  void registerBasicBlock(const KInstruction *instruction,
                          const fingerprint_t &fingerprint);
  void registerEndOfStackFrame(const KFunction *kf,
                               fingerprint_t fingerprintLocalDelta,
                               fingerprint_t fingerprintAllocaDelta);
  StackFrameEntry popFrame();
  bool findInfiniteLoopInFunction() const;
  bool findInfiniteRecursion() const;
  void clear();
  std::size_t getNumberOfStackFrames() const;

  static bool isAllocaAllocationInCurrentStackFrame(const ExecutionState &state,
                                                    const MemoryObject &mo);
  fingerprint_t *getPreviousAllocaDelta(const ExecutionState &state,
                                        const MemoryObject &mo);

  void dumpTrace(llvm::raw_ostream &out = llvm::errs()) const;
};
}

#endif
