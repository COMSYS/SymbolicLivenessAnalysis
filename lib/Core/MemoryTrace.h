#ifndef KLEE_MEMORYTRACE_H
#define KLEE_MEMORYTRACE_H

#include "klee/Internal/Module/KInstruction.h"

#include <utility>
#include <vector>

namespace klee {

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

  struct StackFrameEntry {
    // first index in stack that belongs to next stack frame
    std::size_t index;
    // locals and arguments only visible within this stack frame
    fingerprint_t fingerprintDelta;
    // did this stack frame contain any allocas?
    bool allocas;

    StackFrameEntry(std::size_t index,
                    fingerprint_t fingerprintDelta, bool allocas)
        : index(index), fingerprintDelta(fingerprintDelta), allocas(allocas) {}
  };

  std::vector<MemoryTraceEntry> stack;
  std::vector<StackFrameEntry> stackFrames;

public:
  MemoryTrace() = default;
  MemoryTrace(const MemoryTrace &) = default;

  void registerBasicBlock(const KInstruction *instruction,
                          const fingerprint_t &fingerprint);
  void registerEndOfStackFrame(fingerprint_t fingerprintDelta, bool allocas);
  std::pair<fingerprint_t,bool> popFrame();
  bool findLoop();
  void clear();

  void debugStack();
};
}

#endif
