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

struct MemoryTraceEntry {
  KInstruction *inst;
  std::array<std::uint8_t, 20> hash;

  MemoryTraceEntry(KInstruction *inst, std::array<std::uint8_t, 20> hash) : inst(inst), hash(hash) {}

  bool operator==(const MemoryTraceEntry &rhs) {
    // check KInstruction first (short-circuit evaluation)
    return (inst == rhs.inst && hash == rhs.hash);
  }

  bool operator!=(const MemoryTraceEntry &rhs) {
    return !(operator==(rhs));
  }
};

class MemoryTrace {

private:
  std::vector<MemoryTraceEntry> stack;
  std::vector<std::size_t> stackFrames;

public:
  MemoryTrace() = default;
  MemoryTrace(const MemoryTrace&) = default;

  typedef std::reverse_iterator<std::vector<MemoryTraceEntry>::iterator> stack_iter;

  void registerBasicBlock(KInstruction *instruction, const std::array<std::uint8_t, 20> &hash, bool newStackFrame);
  void popFrame();
  bool findLoop();
  void clear();

  #ifdef MEMORYTRACE_DEBUG
  void debugStack() {
    if(stack.empty()) {
      std::cout << "MemoryTrace is empty" << std::endl;
    } else {
      std::vector<std::size_t> tmpFrames = stackFrames;
      std::cout << "TOP OF MemoryTrace STACK" << std::endl;
      for(std::reverse_iterator<std::vector<MemoryTraceEntry>::iterator> it = stack.rbegin(); it != stack.rend(); ++it) {
        const MemoryTraceEntry &entry = *it;
        const InstructionInfo &ii = *entry.inst->info;
        if(!tmpFrames.empty()) {
          if((std::size_t) (stack.rend() - it) == tmpFrames.back()) {
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
