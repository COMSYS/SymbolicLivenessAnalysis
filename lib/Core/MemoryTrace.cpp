#include "MemoryTrace.h"

namespace klee {

void MemoryTrace::registerBasicBlock(const KInstruction *instruction,
                                     const std::array<std::uint8_t, 20> &hash) {
  MemoryTraceEntry *entry = new MemoryTraceEntry(instruction, hash);
  stack.push_back(*entry);
}

void MemoryTrace::registerEndOfStackFrame(
    std::array<std::uint8_t, 20> hashDifference, bool allocas) {
  StackFrameEntry *entry =
      new StackFrameEntry(stack.size(), hashDifference, allocas);
  stackFrames.push_back(*entry);
}

void MemoryTrace::clear() {
#ifdef MEMORYTRACE_DEBUG
  debugStack();
#endif

  stack.clear();
  stackFrames.clear();

#ifdef MEMORYTRACE_DEBUG
  debugStack();
#endif
}

std::array<std::uint8_t, 20> MemoryTrace::popFrame() {
#ifdef MEMORYTRACE_DEBUG
  debugStack();
#endif

  if (!stackFrames.empty()) {
    StackFrameEntry &sfe = stackFrames.back();
    std::array<std::uint8_t, 20> hashDifference = sfe.hashDifference;

    // delete all PCs and hashes of BasicBlocks
    // that are part of current stack frame
    std::size_t index = sfe.index;
    stack.erase(stack.begin() + index, stack.end());
    // there is no need to modify the indices in
    // stackFrames because lower indices stay the same

    // remove topmost stack frame
    stackFrames.pop_back();

    return hashDifference;
  }

  return {};

#ifdef MEMORYTRACE_DEBUG
  std::cout << "Popping StackFrame" << std::endl;
  debugStack();
#endif
}

bool MemoryTrace::findLoop() {
  MemoryTraceEntry &topEntry = stack.back();

  stack_iter lowerIt = stack.rbegin();
  ++lowerIt; // skip first element
  for (; lowerIt != stack.rend(); ++lowerIt) {
    // TODO: break loop because at some point its impossible that same sequence can be found twice in stack (break at half?)
    // TODO: alloca count for each Stack frame: If count > 0, stop search at SF boundary
    if (topEntry == *lowerIt) {
      // found an entry with same PC and hash
      stack_iter doubleIt = lowerIt;

      // compare predecessors
      stack_iter upperIt = stack.rbegin();
      for (; upperIt <= doubleIt && lowerIt != stack.rend();
           ++upperIt, ++lowerIt) {
        if (*upperIt != *lowerIt) {
          break;
        }
      }

      if (upperIt == doubleIt) {
        // all (lowerIt-upperIt) predecessors are the same => loop found

#ifdef MEMORYTRACE_DEBUG
        std::cout << std::dec << "MemoryTrace: Loop consisting of "
                  << (lowerIt - upperIt) << " BasicBlocks\n";
#endif

        return true;
      } else {
        // reset lowerIt to continue search for
        // other entry with same PC and hash
        lowerIt = doubleIt;
      }
    }
  }

  return false;
}
}
