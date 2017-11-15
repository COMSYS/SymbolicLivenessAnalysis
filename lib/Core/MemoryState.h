#ifndef KLEE_MEMORYSTATE_H
#define KLEE_MEMORYSTATE_H

#include "InfiniteLoopDetectionFlags.h"
#include "Memory.h"
#include "MemoryFingerprint.h"
#include "MemoryTrace.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

// only needed for unregisterLocal
#include "llvm/IR/InstrTypes.h"

#include <cstdint>

namespace llvm {
class BasicBlock;
class Function;
}

namespace klee {
class ExecutionState;

class MemoryState {

private:
  MemoryFingerprint fingerprint;
  MemoryTrace trace;
  const ExecutionState &executionState;
  bool globalAllocationsInCurrentStackFrame = false;

  struct outputFunction {
    bool entered = false;
    llvm::Function *function = nullptr;
  } outputFunction;

  struct libraryFunction {
    bool entered = false;
    llvm::Function *function = nullptr;
    ref<ConstantExpr> address;
    const MemoryObject *mo = nullptr;
    std::size_t bytes = 0;
  } libraryFunction;

  struct basicBlockInfo {
    const llvm::BasicBlock *bb = nullptr;
    const llvm::BasicBlock *prevbb = nullptr;
    std::vector<llvm::Value *> liveRegisters;
  } basicBlockInfo;

  static std::string ExprString(ref<Expr> expr);

  void updateBasicBlockInfo(const llvm::BasicBlock *bb);
  KInstruction *getKInstruction(const llvm::BasicBlock* bb);
  KInstruction *getKInstruction(const llvm::Instruction* inst);
  ref<Expr> getLocalValue(const KInstruction *kinst);
  ref<Expr> getLocalValue(const llvm::Instruction *inst);
  void clearLocal(const KInstruction *kinst);
  void clearLocal(const llvm::Instruction *inst);

  void unregisterConsumedLocals(const llvm::BasicBlock *bb,
                                bool writeToLocalDelta = true);

  void registerLocal(const llvm::Instruction *inst, ref<Expr> value);
  void unregisterLocal(const llvm::Instruction *inst) {
    ref<Expr> value = getLocalValue(inst);
    if (!value.isNull()) {
      registerLocal(inst, value);

      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << "MemoryState: unregister local %" << inst->getName()
                     << ": " << ExprString(value) << " "
                     << "[fingerprint: " << fingerprint.getFingerprintAsString()
                     << "]\n";
      }
    }
  }

public:
  MemoryState() = delete;
  MemoryState(const MemoryState &) = delete;
  MemoryState& operator=(const MemoryState&) = delete;

  MemoryState(const ExecutionState &state) : executionState(state) {}
  MemoryState(const MemoryState &, const ExecutionState &state)
    : executionState(state) {}

  void clearEverything();

  void registerWrite(ref<Expr> address, const MemoryObject &mo,
                     const ObjectState &os, std::size_t bytes);
  void registerWrite(ref<Expr> address, const MemoryObject &mo,
                     const ObjectState &os) {
    registerWrite(address, mo, os, os.size);
  }
  void unregisterWrite(ref<Expr> address, const MemoryObject &mo,
                       const ObjectState &os, std::size_t bytes) {
    if (libraryFunction.entered || outputFunction.entered) {
      return;
    }
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: UNREGISTER\n";
    }

    registerWrite(address, mo, os, bytes);
  }
  void unregisterWrite(const MemoryObject &mo, const ObjectState &os) {
    unregisterWrite(mo.getBaseExpr(), mo, os, os.size);
  }

  void registerLocal(const KInstruction *target, ref<Expr> value);
  void unregisterLocal(const KInstruction *target, ref<Expr> value) {
    if (!libraryFunction.entered && !outputFunction.entered
      && DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << "MemoryState: UNREGISTER LOCAL (KInst)\n";
      }
    }

    registerLocal(target, value);
  }

  void registerArgument(const KFunction *kf, unsigned index, ref<Expr> value);

  void registerExternalFunctionCall();

  void registerBasicBlock(const KInstruction *inst);
  void registerBasicBlock(const llvm::BasicBlock *dst,
                          const llvm::BasicBlock *src);

  bool findLoop();

  bool enterOutputFunction(llvm::Function *f);
  void leaveOutputFunction();
  bool isInOutputFunction(llvm::Function *f);

  bool enterLibraryFunction(llvm::Function *f, ref<ConstantExpr> address,
    const MemoryObject *mo, const ObjectState *os, std::size_t bytes);
  bool isInLibraryFunction(llvm::Function *f);
  const MemoryObject *getLibraryFunctionMemoryObject();
  void leaveLibraryFunction(const ObjectState *os);

  void registerPushFrame(const KFunction *kf);
  void registerPopFrame(const llvm::BasicBlock *returningBB,
                        const llvm::BasicBlock *callerBB);

  void dumpTrace(llvm::raw_ostream &out = llvm::errs()) const {
    trace.dumpTrace(out);
  }
};
}

#endif
