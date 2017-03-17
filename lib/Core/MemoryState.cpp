#include "AddressSpace.h"
#include "DebugInfiniteLoopDetection.h"
#include "Memory.h"
#include "MemoryState.h"

#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Function.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace klee {

void MemoryState::registerExternalFunctionCall() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: external function call\n";
  }

  // it is unknown whether control flow is changed by an external function
  trace.clear();

  // make all previous changes to fingerprint permanent
  fingerprint.resetDelta();
}

void MemoryState::registerAllocation(const MemoryObject &mo) {
  fingerprint.updateUint8(1);
  fingerprint.updateUint64(mo.address);
  fingerprint.updateUint64(mo.size);

  fingerprint.applyToFingerprint();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: processing (de)allocation at address "
                 << mo.address << " of size " << mo.size
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerWrite(ref<Expr> address, const MemoryObject &mo,
                                const ObjectState &os, std::size_t bytes) {
  if (libraryFunction.entered) {
    return;
  }

  ref<ConstantExpr> base = mo.getBaseExpr();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: processing ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  if (!allocasInCurrentStackFrame)
    allocasInCurrentStackFrame = true;

  ref<Expr> offset = mo.getOffsetExpr(address);
  ConstantExpr *concreteOffset = dyn_cast<ConstantExpr>(offset);

  std::uint64_t begin = 0;
  std::uint64_t end = os.size;

  // optimization for concrete offsets: only hash changed indices
  if (concreteOffset) {
    begin = concreteOffset->getZExtValue(64);
    if ((begin + bytes) < os.size) {
      end = begin + bytes;
    }
  }

  for (std::uint64_t i = begin; i < end; i++) {
    // add base address to fingerprint
    fingerprint.updateUint8(2);
    std::uint64_t baseAddress = base->getZExtValue(64);
    fingerprint.updateUint64(baseAddress);

    // add current offset to fingerprint
    fingerprint.updateUint64(i);

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "[+" << i << "] ";
    }

    // add value of byte at offset to fingerprint
    ref<Expr> valExpr = os.read8(i);
    if (ConstantExpr *constant = dyn_cast<ConstantExpr>(valExpr)) {
      // concrete value
      fingerprint.updateUint8(0);
      std::uint8_t value = constant->getZExtValue(8);
      fingerprint.updateUint8(value);
      if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
        llvm::errs() << "0x";
        llvm::errs().write_hex((int)value);
      }
    } else {
      // symbolic value
      fingerprint.updateUint8(1);
      fingerprint.updateExpr(valExpr);
      if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
        llvm::errs() << ExprString(valExpr);
      }
    }

    fingerprint.applyToFingerprint();

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << " [fingerprint: "
                   << fingerprint.getFingerprintAsString() << "]\n";
    }
  }
}

void MemoryState::registerLocal(const KInstruction *target, ref<Expr> value) {
  if (libraryFunction.entered) {
    return;
  }

  fingerprint.updateUint8(3);
  fingerprint.updateUint64(reinterpret_cast<std::intptr_t>(target));

  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateExpr(value);
  }

  fingerprint.applyToFingerprintAndDelta();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    const InstructionInfo &ii = *target->info;
    llvm::errs() << "MemoryState: adding local to instruction "
                 << reinterpret_cast<std::intptr_t>(target)
                 << " (" << ii.file << ":" << ii.line << ":" << ii.id << ")"
                 << ": " << ExprString(value) << "\n"
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerArgument(const KFunction *kf, unsigned index,
                                   ref<Expr> value) {
  if (libraryFunction.entered) {
    return;
  }

  fingerprint.updateUint8(4);
  fingerprint.updateUint64(reinterpret_cast<std::intptr_t>(kf));
  fingerprint.updateUint64(index);

  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateExpr(value);
  }

  fingerprint.applyToFingerprintAndDelta();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: adding argument " << index << " to function "
                 << reinterpret_cast<std::intptr_t>(kf) << ": "
                 << ExprString(value) << "\n"
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerBasicBlock(const KInstruction *inst) {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: BASICBLOCK\n";
  }

  trace.registerBasicBlock(inst, fingerprint.getFingerprint());
}

bool MemoryState::findLoop() {
  if (libraryFunction.entered) {
    // we do not want to find infinite loops in library functions
    return false;
  }

  bool result = trace.findLoop();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    if (result) {
      trace.debugStack();
    }
  }

  return result;
}

bool MemoryState::enterLibraryFunction(llvm::Function *f,
  ref<ConstantExpr> address, const MemoryObject *mo, const ObjectState *os,
  std::size_t bytes) {
  if (libraryFunction.entered) {
    // we can only enter one library function at a time
    klee_warning_once(f, "already entered a library function");
    return false;
  }

  unregisterWrite(address, *mo, *os, bytes);

  libraryFunction.entered = true;
  libraryFunction.function = f;
  libraryFunction.address = address;
  libraryFunction.mo = mo;
  libraryFunction.bytes = bytes;

  return true;
}

bool MemoryState::isInLibraryFunction(llvm::Function *f) {
  if (libraryFunction.entered && f == libraryFunction.function) {
    return true;
  }
  return false;
}

const MemoryObject *MemoryState::getLibraryFunctionMemoryObject() {
  return libraryFunction.mo;
}

void MemoryState::leaveLibraryFunction(const ObjectState *os) {
  libraryFunction.entered = false;
  libraryFunction.function = nullptr;

  registerWrite(libraryFunction.address, *libraryFunction.mo, *os,
    libraryFunction.bytes);
}

void MemoryState::registerPushFrame() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: PUSHFRAME\n";
  }

  trace.registerEndOfStackFrame(fingerprint.getDelta(), allocasInCurrentStackFrame);

  // make locals and arguments "invisible"
  fingerprint.removeDelta();

  // reset stack frame specific information
  allocasInCurrentStackFrame = false;
}

void MemoryState::registerPopFrame() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: POPFRAME\n"
                 << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }

  if (trace.getNumberOfStackFrames() > 0) {
    // remove delta (locals and arguments) of stack frame that is to be left
    fingerprint.removeDelta();

    // make locals and argument "visible" again by
    // applying delta of stack frame that is to be entered
    auto previousFrame = trace.popFrame();
    fingerprint.applyDelta(previousFrame.first);

    allocasInCurrentStackFrame = previousFrame.second;

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "reapplying delta: " << fingerprint.getDeltaAsString()
                   << "\nAllocas: " << allocasInCurrentStackFrame
                   << "\nFingerprint: " << fingerprint.getFingerprintAsString()
                   << "\n";
    }
  } else {
    // no stackframe left to pop

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "no stackframe left in trace\n";
    }
  }
}


std::string MemoryState::ExprString(ref<Expr> expr) {
  std::string result;
  llvm::raw_string_ostream ostream(result);
  expr->print(ostream);
  ostream.flush();
  return result;
}

}
