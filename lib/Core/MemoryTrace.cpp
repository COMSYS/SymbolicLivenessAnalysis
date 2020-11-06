#include "MemoryTrace.h"

#include "ExecutionState.h"
#include "MemoryFingerprint.h"

#include "klee/Module/InstructionInfoTable.h"
#include "klee/Support/ErrorHandling.h"
#include "klee/Support/InfiniteLoopDetectionFlags.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

#include <iomanip>
#include <iterator>
#include <sstream>

/*

   Example: Internal Data Structures of MemoryTrace (llvm::Function* omitted)

   1. registerBasicBlock(inst 1, fingerprint 1);
   2. registerBasicBlock(inst 2, fingerprint 2);
   3. registerBasicBlock(inst 3, fingerprint 3);
   4. registerEndOfStackFrame(d1, d2);
   5. registerBasicBlock(inst 4, fingerprint 4);
   6. registerBasicBlock(inst 5, fingerprint 5);
   7. registerBasicBlock(inst 6, fingerprint 6);
   8. registerEndOfStackFrame(d3, d4);
   9. registerBasicBlock(inst 7, fingerprint 7);


   std::vector<MemoryTraceEntry>
              trace

 #     inst      fingerprint                      std::vector<StackFrameEntry>
   +---------+----------------+                            stackFrames
 6 | inst 7  | fingerprint 7  |
 \==<==============<===================<======+     index   deltas..   #
 5 | inst 6  | fingerprint 6  |   \            \  +-------+----------+
   +---------+----------------+    \            +-|---{ 6 | d3, d4   | 1
 4 | inst 5  | fingerprint 5  |     +- Stack-     +-------+----------+
   +---------+----------------+    /   frame 1  +-|---{ 3 | d1, d2   | 0
 3 | inst 4  | fingerprint 4  |   /            /  +-------+----------+
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
  trace.emplace_back(instruction, fingerprint);
}

void MemoryTrace::registerEndOfStackFrame(
    const llvm::Function *function, fingerprint_t fingerprintLocalDelta,
    fingerprint_t fingerprintAllocaDelta) {

  assert((stackFrames.empty() || trace.size() != stackFrames.back().index) &&
         "cannot have two entries with same index");

  stackFrames.emplace_back(trace.size(), function, fingerprintLocalDelta,
                           fingerprintAllocaDelta);
}

void MemoryTrace::clear() {
  if (DebugInfiniteLoopDetection.isSet(STDERR_TRACE)) {
    dumpTrace();
  }

  trace.clear();
  stackFrames.clear();

  if (DebugInfiniteLoopDetection.isSet(STDERR_TRACE)) {
    dumpTrace();
  }
}

std::size_t MemoryTrace::getNumberOfStackFrames() const {
  return stackFrames.size();
}

MemoryTrace::StackFrameEntry MemoryTrace::popFrame() {
  if (DebugInfiniteLoopDetection.isSet(STDERR_TRACE)) {
    dumpTrace();
  }

  assert(!stackFrames.empty());

  MemoryTrace::StackFrameEntry sfe = stackFrames.back();

  // delete all PCs and fingerprints of BasicBlocks
  // that are part of current stack frame
  std::size_t index = sfe.index;
  trace.erase(trace.begin() + index, trace.end());
  // there is no need to modify the indices in
  // stackFrames because lower indices stay the same

  // remove topmost stack frame
  stackFrames.pop_back();

  if (DebugInfiniteLoopDetection.isSet(STDERR_TRACE)) {
    llvm::errs() << "Popping StackFrame\n";
    dumpTrace();
  }

  return sfe;
}

bool MemoryTrace::findInfiniteLoopInFunction() const {
  if (stackFrames.size() > 0) {
    // current stack frame has always at least one basic block
    assert(stackFrames.back().index < trace.size() &&
           "current stack frame is empty");
  }

  std::size_t topStackFrameEntries = getNumberOfEntriesInCurrentStackFrame();

  // find matching entries within first stack frame
  if (topStackFrameEntries > 1) {
    const MemoryTraceEntry &topEntry = trace.back();
    auto it = trace.rbegin() + 1; // skip first element
    for (; it != trace.rbegin() + topStackFrameEntries; ++it) {
      // iterate over all elements within the first stack frame (but the first)
      if (topEntry == *it) {
        // found an entry with same PC and fingerprint
        return true;
      }
    }
  }
  return false;
}

bool MemoryTrace::findInfiniteRecursion() const {
  if (stackFrames.empty())
    return false;

  assert(stackFrames.back().index < trace.size() &&
         "a stack frame should always have at least one basic block entry");

  // To find infinite recursion, it suffices to find a match of the first
  // entry within a stack frame.
  // This entry is called stack frame base and only contains changes to global
  // memory objects, alloca deltas of previous stack frames and the binding
  // of arguments supplied to a function.
  const MemoryTraceEntry &currentStackFrameBase =
      trace.at(stackFrames.back().index);

  return std::find_if(std::next(stackFrames.rbegin()), stackFrames.rend(),
                      [&](const StackFrameEntry &sfe) {
                        const MemoryTraceEntry &stackFrameBase =
                            trace.at(sfe.index);
                        return currentStackFrameBase == stackFrameBase;
                      }) != stackFrames.rend();
}

bool MemoryTrace::isAllocaAllocationInCurrentStackFrame(
    const ExecutionState &state, const MemoryObject &mo) {
  return (state.stack.size() - 1 == mo.getStackframeIndex());
}

MemoryTrace::fingerprint_t *
MemoryTrace::getPreviousAllocaDelta(const ExecutionState &state,
                                    const MemoryObject &mo) {
  assert(!isAllocaAllocationInCurrentStackFrame(state, mo));

  std::size_t index = mo.getStackframeIndex();

  // Compared to stackFrames, state.stack contains at least one more stack
  // frame, i.e. the currently executed one (top most entry)
  assert(stackFrames.size() + 1 <= state.stack.size());

  // smallest index that is present in MemoryTrace
  std::size_t smallestIndex = state.stack.size() - (stackFrames.size() + 1);
  if (index < smallestIndex) {
    // MemoryTrace has been cleared since the time of allocation
    return nullptr;
  }

  StackFrameEntry &sfe = stackFrames.at(index - smallestIndex);
  return &sfe.fingerprintAllocaDelta;
}

void MemoryTrace::dumpTrace(llvm::raw_ostream &out) const {
  if (trace.empty()) {
    out << "MemoryTrace is empty\n";
  } else {
    std::vector<StackFrameEntry> tmpFrames = stackFrames;
    out << "TOP OF MemoryTrace STACK\n";
    for (auto it = trace.rbegin(); it != trace.rend(); ++it) {
      const MemoryTraceEntry &entry = *it;
      const InstructionInfo &ii = *entry.inst->info;
      if (!tmpFrames.empty()) {
        if (static_cast<std::size_t>(trace.rend() - it) ==
            tmpFrames.back().index) {
          out << "STACKFRAME BOUNDARY " << tmpFrames.size() << "/"
              << stackFrames.size() << "\n";
          tmpFrames.pop_back();
        }
      }
      out << entry.inst << " (" << ii.file << ":" << ii.line << ":" << ii.id
          << "): " << MemoryFingerprint::toString(entry.fingerprint) << "\n";
    }
    out << "BOTTOM OF MemoryTrace STACK\n";
  }
}

} // namespace klee
