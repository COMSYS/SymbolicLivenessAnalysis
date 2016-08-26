#include "MemoryTrace.h"

namespace klee {

void MemoryTrace::registerBasicBlock(KInstruction *instruction, const std::array<std::uint8_t, 20> &hash, bool newStackFrame) {
  MemoryTraceEntry *entry = new MemoryTraceEntry(instruction, hash);
  stack.push_back(*entry);

  if(newStackFrame)
    stackFrames.push_back(stack.size() - 1);
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

void MemoryTrace::popFrame() {
  #ifdef MEMORYTRACE_DEBUG
  debugStack();
  #endif

  if(!stackFrames.empty()) {
    std::size_t index = stackFrames.back();
    stack.erase(stack.begin() + index, stack.end());
    stackFrames.pop_back();
  }
  // there is no need to modify the indices in
  // stackFrames because lower indices stay the same

  #ifdef MEMORYTRACE_DEBUG
  std::cout << "Popping StackFrame" << std::endl;
  debugStack();
  #endif
}


bool MemoryTrace::findLoop() {
  MemoryTraceEntry &topEntry = stack.back();

  stack_iter lowerIt = stack.rbegin();
  ++lowerIt; // skip first element
  for(; lowerIt != stack.rend(); ++lowerIt) {
    // TODO: break loop because at some point its impossible that same sequence can be found twice in stack (break at half?)
    // TODO: alloca count for each Stack frame: If count > 0, stop search at SF boundary
    if(topEntry == *lowerIt) {
      // found an entry with same PC and hash
      stack_iter doubleIt = lowerIt;

      // compare predecessors
      stack_iter upperIt = stack.rbegin();
      for(; upperIt <= doubleIt && lowerIt != stack.rend(); ++upperIt, ++lowerIt) {
        if(*upperIt != *lowerIt) {
          break;
        }
      }

      if(upperIt == doubleIt) {
        // all (upperIt-lowerIt) predecessors are the same => loop found
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
