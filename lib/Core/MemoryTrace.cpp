#include "DebugInfiniteLoopDetection.h"
#include "MemoryTrace.h"

#include "klee/Internal/Module/InstructionInfoTable.h"

#include "llvm/Support/raw_ostream.h"

#include <iomanip>
#include <iterator>
#include <sstream>

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
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    debugStack();
  }

  stack.clear();
  stackFrames.clear();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    debugStack();
  }
}

std::array<std::uint8_t, 20> MemoryTrace::popFrame() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    debugStack();
  }

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

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    llvm::errs() << "Popping StackFrame\n";
    debugStack();
  }

  return {};
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

        if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
          llvm::errs() << "MemoryTrace: Loop consisting of "
                       << (lowerIt - upperIt) << " BasicBlocks\n";
        }

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


void MemoryTrace::debugStack() {
  if (stack.empty()) {
    llvm::errs() << "MemoryTrace is empty\n";
  } else {
    std::vector<StackFrameEntry> tmpFrames = stackFrames;
    llvm::errs() << "TOP OF MemoryTrace STACK\n";
    for (stack_iter it = stack.rbegin(); it != stack.rend(); ++it) {
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
                   << ii.id << "): " << Sha1String(entry.hash) << "\n";
    }
    llvm::errs() << "BOTTOM OF MemoryTrace STACK\n";
  }
}

std::string
MemoryTrace::Sha1String(const std::array<std::uint8_t, 20> &buffer) {
  std::stringstream result;
  for (std::array<std::uint8_t, 20>::const_iterator iter = buffer.cbegin();
       iter != buffer.cend(); ++iter) {
    result << std::hex << std::setfill('0') << std::setw(2);
    result << static_cast<unsigned int>(*iter);
  }
  return result.str();
}
}
