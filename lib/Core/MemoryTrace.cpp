#include "InfiniteLoopDetectionFlags.h"
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
              trace

       inst      fingerprint                      std::vector<StackFrameEntry>
   +---------+----------------+                            stackFrames
 6 | inst 7  | fingerprint 7  |
 \==<==============<===================<======+     index   delta    glAlloc
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
  trace.emplace_back(instruction, fingerprint);
}

void MemoryTrace::registerEndOfStackFrame(fingerprint_t fingerprintDelta,
                                          bool globalAllocation) {
  stackFrames.emplace_back(trace.size(), fingerprintDelta, globalAllocation);
}

void MemoryTrace::clear() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    dumpTrace();
  }

  trace.clear();
  stackFrames.clear();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    dumpTrace();
  }
}

std::size_t MemoryTrace::getNumberOfStackFrames() {
  return stackFrames.size();
}

std::pair<MemoryFingerprint::fingerprint_t,bool> MemoryTrace::popFrame() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    dumpTrace();
  }

  if (!stackFrames.empty()) {
    StackFrameEntry &sfe = stackFrames.back();
    fingerprint_t fingerprintDelta = sfe.fingerprintDelta;
    bool globalAllocation = sfe.globalAllocation;

    // delete all PCs and fingerprints of BasicBlocks
    // that are part of current stack frame
    std::size_t index = sfe.index;
    trace.erase(trace.begin() + index, trace.end());
    // there is no need to modify the indices in
    // stackFrames because lower indices stay the same

    // remove topmost stack frame
    stackFrames.pop_back();

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
      llvm::errs() << "Popping StackFrame\n";
      dumpTrace();
    }

    return std::make_pair(fingerprintDelta, globalAllocation);
  }

  return {};
}

bool MemoryTrace::findLoop() {
  if(stackFrames.size() > 0) {
    // current stack frame has always at least one basic block
    assert(stackFrames.back().index < trace.size() &&
      "current stack frame is empty");
  }

  auto stackFramesIt = stackFrames.rbegin();
  std::size_t topStackFrameBoundary = 0;
  if(stackFramesIt != stackFrames.rend()) {
    // first index that belongs to current stack frame
    topStackFrameBoundary = stackFramesIt->index;
  }

  // calculate number of entries within first stack frame
  std::size_t topStackFrameEntries = trace.size() - topStackFrameBoundary;

  // Phase 1:
  // find matching entries within first stack frame
  if (topStackFrameEntries > 1) {
    MemoryTraceEntry &topEntry = trace.back();
    auto it = trace.rbegin() + 1; // skip first element
    for (; it != trace.rbegin() + topStackFrameEntries; ++it) {
      // iterate over all elements within the first stack frame (but the first)
      if (topEntry == *it) {
        // found an entry with same PC and fingerprint

        // TODO: remove?
        dumpTrace();

        return true;
      }
    }
  }

  // Phase 2:
  // For all following stack frames, it suffices to find a match of the first
  // entry within a stack frame.
  // This entry is called stack frame base and only contains changes to global
  // memory objects and the binding of arguments supplied to a function.
  if(stackFrames.size() > 0) {
    MemoryTraceEntry &topStackFrameBase = trace.at(topStackFrameBoundary);

    for (auto it = stackFrames.rbegin() + 1; it != stackFrames.rend(); ++it) {
      // iterate over all stack frames (but the first)

      if(it->globalAllocation) {
        // Allocation addresses can differ between allocations which leads to
        // different fingerprints for two otherwise equal iterations of an
        // infinite loop containing an allocation.
        // Global allocations influence every fingerprint obtained after the
        // allocation took place. Thus, we cannot detect any infinite loop in
        // this case.
        // In contrast, local allocations (allocas) are not harmful, as these
        // only influence every fingerprint within the same stack frame and are
        // made after the stack frame base is registered. That is, they are not
        // part of the fingerprints compared in the following.
        klee_warning_once(trace[topStackFrameBoundary-1].inst,
          "previous stack frame contains global allocation, "
          "aborting search for infinite loops at this location");
        return false;
      }

      MemoryTraceEntry &stackFrame = trace.at(it->index);
      if (topStackFrameBase == stackFrame) {
        // PC and iterator are the same at stack frame base

        // TODO: remove?
        dumpTrace();

        return true;
      }
    }
  }

  return false;
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
        if ((std::size_t)(trace.rend() - it) == tmpFrames.back().index) {
          out << "STACKFRAME BOUNDARY " << tmpFrames.size() << "/"
                       << stackFrames.size() << "\n";
          tmpFrames.pop_back();
        }
      }
      out << entry.inst << " (" << ii.file << ":" << ii.line << ":"
                   << ii.id << "): "
                   << MemoryFingerprint::toString(entry.fingerprint) << "\n";
    }
    out << "BOTTOM OF MemoryTrace STACK\n";
  }
}

}
