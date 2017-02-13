#ifndef KLEE_MEMORYTRACE_H
#define KLEE_MEMORYTRACE_H

#include "klee/Internal/Module/KInstruction.h"

#include <array>
#include <vector>

namespace klee {

class MemoryTrace {

private:
  struct MemoryTraceEntry {
    const KInstruction *inst;
    std::array<std::uint8_t, 20> hash;

    MemoryTraceEntry(const KInstruction *inst,
                     std::array<std::uint8_t, 20> hash)
        : inst(inst), hash(hash) {}

    bool operator==(const MemoryTraceEntry &rhs) {
      // check KInstruction first (short-circuit evaluation)
      return (inst == rhs.inst && hash == rhs.hash);
    }

    bool operator!=(const MemoryTraceEntry &rhs) { return !(operator==(rhs)); }
  };

  struct StackFrameEntry {
    // first index in stack that belongs to next stack frame
    std::size_t index;
    // locals and arguments only visible within this stack frame
    std::array<std::uint8_t, 20> hashDifference;
    // did this stack frame contain any allocas?
    bool allocas;

    StackFrameEntry(std::size_t index,
                    std::array<std::uint8_t, 20> hashDifference, bool allocas)
        : index(index), hashDifference(hashDifference), allocas(allocas) {}
  };

  std::vector<MemoryTraceEntry> stack;
  std::vector<StackFrameEntry> stackFrames;

public:
  MemoryTrace() = default;
  MemoryTrace(const MemoryTrace &) = default;

  void registerBasicBlock(const KInstruction *instruction,
                          const std::array<std::uint8_t, 20> &hash);
  void registerEndOfStackFrame(std::array<std::uint8_t, 20> hashDifference,
                               bool allocas);
  std::array<std::uint8_t, 20> popFrame();
  bool findLoop();
  void clear();

  void debugStack();
  std::string Sha1String(const std::array<std::uint8_t, 20> &buffer);
};
}

#endif
