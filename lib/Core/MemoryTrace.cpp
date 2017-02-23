#include "DebugInfiniteLoopDetection.h"
#include "MemoryFingerprint.h"
#include "MemoryTrace.h"

#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Support/raw_ostream.h"

#include <iomanip>
#include <iterator>
#include <sstream>

namespace klee {

void MemoryTrace::registerBasicBlock(const KInstruction *instruction,
                                     const fingerprint_t &fingerprint) {
  MemoryTraceEntry *entry = new MemoryTraceEntry(instruction, fingerprint);
  stack.push_back(*entry);
}

void MemoryTrace::registerEndOfStackFrame(fingerprint_t fingerprintDelta,
                                          bool allocas) {
  StackFrameEntry *entry =
    new StackFrameEntry(stack.size(), fingerprintDelta, allocas);
  stackFrames.push_back(*entry);
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
  size_t currentStackFrameBoundary = 0;
  if(stackFramesIt != stackFrames.rend()) {
    // first index that belongs to current stack frame
    currentStackFrameBoundary = stackFramesIt->index;
  }

  MemoryTraceEntry &topEntry = stack.back();
  auto it = stack.rbegin();
  ++it; // skip first element
  for (; it != stack.rend(); ++it) {
    if (topEntry == *it) {
      // found an entry with same PC and fingerprint
      return true;
    }

    size_t index = std::distance(stack.begin(), it.base()) - 1;
    if(stackFramesIt != stackFrames.rend() &&
       currentStackFrameBoundary == index) {
      // entering new stack frame in next iteration
      if(stackFramesIt->allocas) {
        // stack frame contained allocas, thus we cannot find any further match
        klee_warning_once(stack[currentStackFrameBoundary-1].inst,
          "previous stack frame contains alloca, "
          "aborting search for infinite loops at this location");
        return false;
      } else {
        // find boundary of previous stack frame
        ++stackFramesIt;
        currentStackFrameBoundary = stackFramesIt->index;
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
