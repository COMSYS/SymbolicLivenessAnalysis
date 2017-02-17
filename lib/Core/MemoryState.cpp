#include "AddressSpace.h"
#include "DebugInfiniteLoopDetection.h"
#include "Memory.h"
#include "MemoryState.h"

#include "llvm/Support/raw_ostream.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace klee {

void MemoryState::registerExternalFunctionCall() {
  // it is unknown whether control flow is changed by an external function
  trace.clear();
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

void MemoryState::registerWrite(ref<Expr> base, const MemoryObject &mo,
                                const ObjectState &os) {

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: processing ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  if (!allocasInCurrentStackFrame)
    allocasInCurrentStackFrame = true;

  ConstantExpr *constantBase = dyn_cast<ConstantExpr>(base);

  for (std::uint64_t offset = 0; offset < os.size; offset++) {
    // add base address to fingerprint
    if (constantBase) {
      // concrete address
      fingerprint.updateUint8(2);
      assert(constantBase->getWidth() <= 64 && "address greater than 64 bit!");
      std::uint64_t address = constantBase->getZExtValue(64);
      fingerprint.updateUint64(address);
    } else {
      // symbolic address
      fingerprint.updateUint8(3);
      fingerprint.updateExpr(base);
    }

    // add current offset to fingerprint
    fingerprint.updateUint64(offset);

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "[+" << offset << "] ";
    }

    // add value of byte at offset to fingerprint
    ref<Expr> valExpr = os.read8(offset);
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

void MemoryState::registerConstraint(ref<Expr> condition) {
  fingerprint.updateUint8(4);
  fingerprint.updateExpr(condition);
  fingerprint.applyToFingerprint();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: adding new constraint: "
                 << ExprString(condition)
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerLocal(const KInstruction *target, ref<Expr> value) {
  fingerprint.updateUint8(5);
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
    llvm::errs() << "MemoryState: adding local  to instruction "
                 << reinterpret_cast<std::intptr_t>(target)
                 << ": " << ExprString(value) << "\n"
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerArgument(const KFunction *kf, unsigned index,
                                   ref<Expr> value) {
  fingerprint.updateUint8(6);
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
  bool result = trace.findLoop();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_TRACE)) {
    if (result) {
      trace.debugStack();
    }
  }

  return result;
}

void MemoryState::registerPushFrame() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: PUSHFRAME\n";
  }

  trace.registerEndOfStackFrame(fingerprint.getDelta(), allocasInCurrentStackFrame);

  // make locals and parameters "invisible"
  fingerprint.removeDelta();

  // reset stack frame specific information
  allocasInCurrentStackFrame = false;
}

void MemoryState::registerPopFrame() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: POPFRAME\n";
  }

  // make locals and parameters "visible" again
  MemoryFingerprint::fingerprint_t delta = trace.popFrame();
  fingerprint.applyDelta(delta);
}


std::string MemoryState::ExprString(ref<Expr> expr) {
  std::string result;
  llvm::raw_string_ostream ostream(result);
  expr->print(ostream);
  ostream.flush();
  return result;
}

}
