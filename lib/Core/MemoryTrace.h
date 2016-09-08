#ifndef KLEE_MEMORYTRACE_H
#define KLEE_MEMORYTRACE_H

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include <array>
#include <iterator>
#include <vector>

#ifdef MEMORYTRACE_DEBUG
#include <iostream>
#include <iomanip>
#include <sstream>
#endif

namespace klee {

class MemoryTrace {

private:
  struct MemoryTraceEntry {
    const KInstruction * inst;
    std::array<std::uint8_t, 20> hash;

    MemoryTraceEntry(const KInstruction *inst, std::array<std::uint8_t, 20> hash)
        : inst(inst), hash(hash) {}

    bool operator==(const MemoryTraceEntry &rhs) {
      // check KInstruction first (short-circuit evaluation)
      return (inst == rhs.inst && hash == rhs.hash);
    }

    bool operator!=(const MemoryTraceEntry &rhs) {
      return !(operator==(rhs));
    }
  };

  struct StackFrameEntry {
    std::size_t index;
    std::array<std::uint8_t, 20> hashDifference;
    bool allocas;

    StackFrameEntry(std::size_t index, std::array<std::uint8_t, 20> hashDifference, bool allocas)
        : index(index), hashDifference(hashDifference), allocas(allocas) {}
  };

  std::vector<MemoryTraceEntry> stack;
  std::vector<StackFrameEntry> stackFrames;

public:
  MemoryTrace() = default;
  MemoryTrace(const MemoryTrace&) = default;

  typedef std::reverse_iterator<std::vector<MemoryTraceEntry>::iterator> stack_iter;

  void registerBasicBlock(const KInstruction *instruction, const std::array<std::uint8_t, 20> &hash);
  void registerEndOfStackFrame(std::array<std::uint8_t, 20> hashDifference, bool allocas);
  std::array<std::uint8_t, 20> popFrame();
  bool findLoop();
  void clear();

  #ifdef MEMORYTRACE_DEBUG
  void debugStack() {
    if(stack.empty()) {
      std::cout << "MemoryTrace is empty" << std::endl;
    } else {
      std::vector<StackFrameEntry> tmpFrames = stackFrames;
      std::cout << "TOP OF MemoryTrace STACK" << std::endl;
      for(std::reverse_iterator<std::vector<MemoryTraceEntry>::iterator> it = stack.rbegin(); it != stack.rend(); ++it) {
        const MemoryTraceEntry &entry = *it;
        const InstructionInfo &ii = *entry.inst->info;
        if(!tmpFrames.empty()) {
          if((std::size_t) (stack.rend() - it) == tmpFrames.back().index) {
            std::cout << "STACKFRAME BOUNDARY " << tmpFrames.size() << "/" << stackFrames.size() << std::endl;
            tmpFrames.pop_back();
          }
        }
        std::cout << entry.inst << " (" << ii.file
            << ":" << ii.line << ":" << ii.id << "): "
            << Sha1String(entry.hash) << std::endl;
      }
      std::cout << "BOTTOM OF MemoryTrace STACK" << std::endl;
    }
  }

  std::string Sha1String(const std::array<std::uint8_t, 20> &buffer) {
    std::stringstream result;
    for (std::array<std::uint8_t, 20>::const_iterator iter = buffer.cbegin(); iter != buffer.cend(); ++iter) {
        result << std::hex << std::setfill('0') << std::setw(2);
        result << static_cast<unsigned int>(*iter);
    }
    return result.str();
  }
  #endif


};
}

#endif
