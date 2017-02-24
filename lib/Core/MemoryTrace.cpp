#include "DebugInfiniteLoopDetection.h"
#include "MemoryFingerprint.h"
#include "MemoryTrace.h"

#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Support/raw_ostream.h"

#include <iomanip>
#include <iterator>
#include <sstream>

/*

   Example: Internal Data Structures of MemoryTrace

   1. registerBasicBlock(inst 1, fingerprint 1);
   2. registerBasicBlock(inst 2, fingerprint 2);
   3. registerBasicBlock(inst 3, fingerprint 3);
   4. registerEndOfStackFrame(delta 1, true);
   5. registerBasicBlock(inst 4, fingerprint 4);
   6. registerBasicBlock(inst 5, fingerprint 5);
   7. registerBasicBlock(inst 6, fingerprint 6);
   8. registerEndOfStackFrame(delta 2, false);
   9. registerBasicBlock(inst 7, fingerprint 7);


   std::vector<MemoryTraceEntry>
              stack

       inst      fingerprint                      std::vector<StackFrameEntry>
   +---------+----------------+                            stackFrames
 6 | inst 7  | fingerprint 7  |
 \==<==============<===================<======+     index   delta    allocas
 5 | inst 6  | fingerprint 6  |   \            \  +-------+---------+-------+
   +---------+----------------+    \            +-|---{ 6 | delta 2 | false | 1
 4 | inst 5  | fingerprint 5  |     +- Stack-     +-------+---------+-------+
   +---------+----------------+    /   frame 1  +-|---{ 3 | delta 1 | true  | 0
 3 | inst 4  | fingerprint 4  |   /            /  +-------+---------+-------+
 \==<==============<===================<======+         |
 2 | inst 3  | fingerprint 3  |   \                     +- index marks the first
   +---------+----------------+    \                       entry that belongs
 1 | inst 2  | fingerprint 2  |     +- Stack-              to the next stack
   +---------+----------------+    /   frame 0             frame
 0 | inst 1  | fingerprint 1  |   /
   +---------+----------------+--+

*/

namespace klee {

void MemoryTrace::registerBasicBlock(const KInstruction *instruction,
                                     const fingerprint_t &fingerprint) {
  stack.emplace_back(instruction, fingerprint);
}

void MemoryTrace::registerEndOfStackFrame(fingerprint_t fingerprintDelta,
                                          bool allocas) {
  stackFrames.emplace_back(stack.size(), fingerprintDelta, allocas);
}

void MemoryTrace::clear() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    debugStack();
  }

  stack.clear();
  stackFrames.clear();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    debugStack();
  }
}

std::size_t MemoryTrace::getNumberOfStackFrames() {
  return stackFrames.size();
}

std::pair<MemoryFingerprint::fingerprint_t,bool> MemoryTrace::popFrame() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    debugStack();
  }

  if (!stackFrames.empty()) {
    StackFrameEntry &sfe = stackFrames.back();
    fingerprint_t fingerprintDelta = sfe.fingerprintDelta;
    bool allocas = sfe.allocas;

    // delete all PCs and fingerprints of BasicBlocks
    // that are part of current stack frame
    std::size_t index = sfe.index;
    stack.erase(stack.begin() + index, stack.end());
    // there is no need to modify the indices in
    // stackFrames because lower indices stay the same

    // remove topmost stack frame
    stackFrames.pop_back();

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
      llvm::errs() << "Popping StackFrame\n";
      debugStack();
    }

    return std::make_pair(fingerprintDelta, allocas);
  }

  return {};
}

bool MemoryTrace::findLoop() {
  if(stackFrames.size() > 0) {
    // current stack frame has always at least one basic block
    assert(stackFrames.back().index < stack.size() &&
      "current stack frame is empty");
  }

  auto stackFramesIt = stackFrames.rbegin();
  std::size_t topStackFrameBoundary = 0;
  if(stackFramesIt != stackFrames.rend()) {
    // first index that belongs to current stack frame
    topStackFrameBoundary = stackFramesIt->index;
  }

  // calculate number of entries within first stack frame
  std::size_t topStackFrameEntries = stack.size() - topStackFrameBoundary;

  // Phase 1:
  // find matching entries within first stack frame
  if (topStackFrameEntries > 1) {
    MemoryTraceEntry &topEntry = stack.back();
    auto it = stack.rbegin() + 1; // skip first element
    for (; it != stack.rbegin() + topStackFrameEntries; ++it) {
      // iterate over all elements within the first stack frame (but the first)
      if (topEntry == *it) {
        // found an entry with same PC and fingerprint
        return true;
      }
    }
  }

  // Phase 2:
  // for all following stack frames it suffices to find a match of the first
  // entry within a stack frame
  if(stackFrames.size() > 0) {
    MemoryTraceEntry &topStackFrameBase = stack.at(topStackFrameBoundary);

    for (auto it = stackFrames.rbegin() + 1; it != stackFrames.rend(); ++it) {
      // iterate over all stack frames (but the first)

      if(it->allocas) {
        // stack frame containes allocas, thus we cannot find any further match
        klee_warning_once(stack[topStackFrameBoundary-1].inst,
          "previous stack frame contains alloca, "
          "aborting search for infinite loops at this location");
        return false;
      }

      MemoryTraceEntry &stackFrame = stack.at(it->index);
      if (topStackFrameBase == stackFrame) {
        // PC and iterator are the same at stack frame base
        return true;
      }
    }
  }

  return false;
}


void MemoryTrace::debugStack() {
  if (stack.empty()) {
    llvm::errs() << "MemoryTrace is empty\n";
  } else {
    std::vector<StackFrameEntry> tmpFrames = stackFrames;
    llvm::errs() << "TOP OF MemoryTrace STACK\n";
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      const MemoryTraceEntry &entry = *it;
      const InstructionInfo &ii = *entry.inst->info;
      if (!tmpFrames.empty()) {
        if ((std::size_t)(stack.rend() - it) == tmpFrames.back().index) {
          llvm::errs() << "STACKFRAME BOUNDARY " << tmpFrames.size() << "/"
                       << stackFrames.size() << "\n";
          tmpFrames.pop_back();
        }
      }
      llvm::errs() << entry.inst << " (" << ii.file << ":" << ii.line << ":"
                   << ii.id << "): "
                   << MemoryFingerprint::toString(entry.fingerprint) << "\n";
    }
    llvm::errs() << "BOTTOM OF MemoryTrace STACK\n";
  }
}

}
