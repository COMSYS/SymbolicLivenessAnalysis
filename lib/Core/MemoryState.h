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
    std::vector<llvm::Value *> liveRegisters;
  } basicBlockInfo;

  bool deferredBasicBlock = false;

  static std::string ExprString(ref<Expr> expr);

  void populateLiveRegisters(const llvm::BasicBlock *bb);
  KInstruction *getKInstruction(const ExecutionState *state,
                                const llvm::BasicBlock* bb);
  KInstruction *getKInstruction(const ExecutionState *state,
                                const llvm::Instruction* inst);
  ref<Expr> getLocalValue(const ExecutionState *state,
                          const KInstruction *kinst);
  ref<Expr> getLocalValue(const ExecutionState *state,
                          const llvm::Instruction *inst);
  void clearLocal(const ExecutionState *state, const KInstruction *kinst);
  void clearLocal(const ExecutionState *state, const llvm::Instruction *inst);

  void removeConsumedLocals(const ExecutionState *state, llvm::BasicBlock *bb,
                            bool unregister = true);

  void registerLocal(const llvm::Instruction *inst, ref<Expr> value);
  void unregisterLocal(const ExecutionState *state,
                       const llvm::Instruction *inst)
  {
    ref<Expr> value = getLocalValue(state, inst);
    if (!value.isNull()) {
      registerLocal(inst, value);

      if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
        llvm::errs() << "MemoryState: unregister local %" << inst->getName()
                     << ": " << ExprString(value) << " "
                     << "[fingerprint: " << fingerprint.getFingerprintAsString()
                     << "]\n";
      }
    }
  }

public:
  MemoryState() = default;
  MemoryState(const MemoryState &) = default;

  void registerAllocation(const MemoryObject &mo);
  void registerDeallocation(const MemoryObject &mo) {
    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "MemoryState: DEALLOCATION\n";
    }

    registerAllocation(mo);
  }

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
    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
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
      && optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
        llvm::errs() << "MemoryState: UNREGISTER LOCAL (KInst)\n";
      }
    }

    registerLocal(target, value);
  }

  void registerArgument(const KFunction *kf, unsigned index, ref<Expr> value);

  void registerExternalFunctionCall();

  bool registerBasicBlock(const KInstruction *inst);
  bool registerBasicBlock(const ExecutionState *state,
                          llvm::BasicBlock *dst,
                          llvm::BasicBlock *src);
  bool registerDeferredBasicBlock(const KInstruction *inst);

  bool findLoop();

  bool enterOutputFunction(llvm::Function *f);
  void leaveOutputFunction();
  bool isInOutputFunction(llvm::Function *f);

  bool enterLibraryFunction(llvm::Function *f, ref<ConstantExpr> address,
    const MemoryObject *mo, const ObjectState *os, std::size_t bytes);
  bool isInLibraryFunction(llvm::Function *f);
  const MemoryObject *getLibraryFunctionMemoryObject();
  void leaveLibraryFunction(const ObjectState *os);

  void registerPushFrame();
  void registerPopFrame(const ExecutionState *state, KInstruction *ki);

  void dumpTrace(llvm::raw_ostream &out = llvm::errs()) const {
    trace.dumpTrace(out);
  }
};
}

#endif
