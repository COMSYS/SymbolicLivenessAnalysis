#include "AddressSpace.h"
#include "InfiniteLoopDetectionFlags.h"
#include "Memory.h"
#include "MemoryState.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace klee {

void MemoryState::clearEverything() {
  trace.clear();
  fingerprint.discardEverything();
}

void MemoryState::registerExternalFunctionCall() {
  if (outputFunction.entered) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: external function call\n";
  }

  // it is unknown whether control flow is changed by an external function, so
  // we cannot detect infinite loop iterations that started before this call
  trace.clear();
  fingerprint.discardEverything();
}

void MemoryState::registerWrite(ref<Expr> address, const MemoryObject &mo,
                                const ObjectState &os, std::size_t bytes) {
  if (libraryFunction.entered || outputFunction.entered) {
    return;
  }

  ref<ConstantExpr> base = mo.getBaseExpr();

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: processing "
                 << (mo.isLocal ? "local " : "global ")
                 << "ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  ref<Expr> offset = mo.getOffsetExpr(address);
  ConstantExpr *concreteOffset = dyn_cast<ConstantExpr>(offset);

  std::uint64_t begin = 0;
  std::uint64_t end = os.size;

  bool isLocal = false;
  MemoryFingerprint::fingerprint_t *externalDelta = nullptr;

  if (mo.isLocal) {
    isLocal = true;
    if (!trace.isAllocaAllocationInCurrentStackFrame(executionState, mo)) {
      externalDelta = trace.findAllocaAllocationStackFrame(executionState, mo);
      if (externalDelta == nullptr) {
        isLocal = false;
      }
    }
  }

  if (!globalAllocationsInCurrentStackFrame && !isLocal)
    globalAllocationsInCurrentStackFrame = true;

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

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "[+" << i << "] ";
    }

    // add value of byte at offset to fingerprint
    ref<Expr> valExpr = os.read8(i);
    if (ConstantExpr *constant = dyn_cast<ConstantExpr>(valExpr)) {
      // concrete value
      fingerprint.updateUint8(0);
      std::uint8_t value = constant->getZExtValue(8);
      fingerprint.updateUint8(value);
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << "0x";
        llvm::errs().write_hex((int)value);
      }
    } else {
      // symbolic value
      fingerprint.updateUint8(1);
      fingerprint.updateExpr(valExpr);
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << ExprString(valExpr);
      }
    }

    if (isLocal) {
      if (externalDelta == nullptr) {
        fingerprint.applyToFingerprintAllocaDelta();
      } else {
        fingerprint.applyToFingerprintAllocaDelta(*externalDelta);
      }
    } else {
      fingerprint.applyToFingerprint();
    }

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      if (i % 10 == 9) {
        llvm::errs() << "\n";
      } else {
        llvm::errs() << " ";
      }
    }
  }
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << " [fingerprint: "
                 << fingerprint.getFingerprintAsString() << "]\n";
  }
}

void MemoryState::registerLocal(const KInstruction *target, ref<Expr> value) {
  if (libraryFunction.entered || outputFunction.entered) {
    return;
  }

  if (value.isNull()) {
    return;
  }

  llvm::Instruction *inst = target->inst;
  if (inst->getParent() != basicBlockInfo.bb) {
    populateLiveRegisters(inst->getParent());
  }
  llvm::Value *instValue = static_cast<llvm::Value *>(inst);
  if (std::find(basicBlockInfo.liveRegisters.begin(),
                basicBlockInfo.liveRegisters.end(),
                instValue) == basicBlockInfo.liveRegisters.end())
  {
    return;
  }

  registerLocal(inst, value);

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: register local %" << target->inst->getName()
                 << ": " << ExprString(value)
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerLocal(const llvm::Instruction *inst, ref<Expr> value)
{
  if (libraryFunction.entered || outputFunction.entered) {
    return;
  }

  if (value.isNull()) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "registerLocal(%" << inst->getName() << ", value)\n";
  }

  fingerprint.updateUint8(3);
  fingerprint.updateUint64(reinterpret_cast<std::intptr_t>(inst));

  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateExpr(value);
  }

  fingerprint.applyToFingerprintLocalDelta();
}

void MemoryState::registerArgument(const KFunction *kf, unsigned index,
                                   ref<Expr> value) {
  if (libraryFunction.entered || outputFunction.entered) {
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

  fingerprint.applyToFingerprintLocalDelta();

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: adding argument " << index << " to function "
                 << reinterpret_cast<std::intptr_t>(kf) << ": "
                 << ExprString(value) << "\n"
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}


void MemoryState::populateLiveRegisters(const llvm::BasicBlock *bb) {
  if (basicBlockInfo.bb != bb) {
    basicBlockInfo.prevbb = basicBlockInfo.bb; // save previous BasicBlock
    basicBlockInfo.bb = bb;
    basicBlockInfo.liveRegisters.clear();

    const llvm::Instruction *term = bb->getTerminator();
    if (llvm::MDNode *liveRegisters = term->getMetadata("liveregister.live")) {
      for (std::size_t i = 0; i < liveRegisters->getNumOperands(); ++i) {
        llvm::Value *live = liveRegisters->getOperand(i);
        basicBlockInfo.liveRegisters.push_back(live);
      }
    }
  }
}

void MemoryState::registerBasicBlock(const KInstruction *inst) {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: BASICBLOCK [fingerprint: "
                 << fingerprint.getFingerprintAsString() << "]\n";
  }

  llvm::BasicBlock *bb = inst->inst->getParent();
  populateLiveRegisters(bb);
  trace.registerBasicBlock(inst, fingerprint.getFingerprint());
}

void MemoryState::removeConsumedLocals(const llvm::BasicBlock *bb,
                                       bool unregister) {

  populateLiveRegisters(bb);

  std::vector<llvm::Value *> consumedRegs;

    const llvm::Instruction *ti = bb->getTerminator();
    llvm::MDNode *consumedRegisters = ti->getMetadata("liveregister.consumed");
    if (consumedRegisters != nullptr) {
      for (std::size_t i = 0; i < consumedRegisters->getNumOperands(); ++i) {
        llvm::Value *consumed = consumedRegisters->getOperand(i);
        consumedRegs.push_back(consumed);
        llvm::Instruction *inst = static_cast<llvm::Instruction *>(consumed);
        if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
          llvm::errs() << "MemoryState: consumed by previous basic block: "
                       << inst->getName() << "\n";
        }
        if (unregister) {
          // remove local from delta
          unregisterLocal(inst);
        }
        // set local within KLEE to zero to mark them as dead
        clearLocal(inst);
      }
    }

  for (auto it = bb->begin(), e = bb->end(); it != e; ++it) {
    const llvm::Instruction &i = *it;
    if (i.getOpcode() == llvm::Instruction::PHI) {
      for (auto use = i.op_begin(), e = i.op_end(); use != e; ++use) {
        if (std::find(basicBlockInfo.liveRegisters.begin(),
                      basicBlockInfo.liveRegisters.end(),
                      use->get()) != basicBlockInfo.liveRegisters.end())
        {
          // register is live, do unregister or clear
          continue;
        } else if (std::find(consumedRegs.begin(),
                             consumedRegs.end(),
                             use->get()) != consumedRegs.end())
        {
          // register is consumed, do unregister or clear
          continue;
        } else {
          llvm::Instruction *inst = dyn_cast<llvm::Instruction>(use->get());
          if (!inst) continue;
          if (unregister) {
            // remove local from delta
            unregisterLocal(inst);
          }
          // set local within KLEE to zero to mark them as dead
          clearLocal(inst);
        }
      }
    } else {
      // http://releases.llvm.org/3.4.2/docs/LangRef.html#phi-instruction
      // "There must be no non-phi instructions between the start of a basic
      //  block and the PHI instructions: i.e. PHI instructions must be first
      //  in a basic block."
      // Thus, we can abort as soon as we encounter a non-phi instruction
      break;
    }
  }
}

void MemoryState::registerBasicBlock(const llvm::BasicBlock *dst,
                                     const llvm::BasicBlock *src) {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "registerBasicBlock " << dst->getName()
                 << " (coming from " << src->getName() << ")\n";
  }

  removeConsumedLocals(src);

  populateLiveRegisters(dst);

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "live variables at the end of " << dst->getName() << ": {";
    for (std::size_t i = 0; i < basicBlockInfo.liveRegisters.size(); ++i) {
      llvm::Value *liveRegister = basicBlockInfo.liveRegisters.at(i);

      llvm::errs() << "%" << liveRegister->getName();
      if (i + 1 < basicBlockInfo.liveRegisters.size()) {
        llvm::errs() << ", ";
      }
    }
    llvm::errs() << "}\n";
  }


  // kill registers
  //
  // liveregister.killed:
  //
  // +-- edges
  // |
  // (
  //   (precedingBasicBlock1, (killedRegister1, killedRegister2)), // edge 1
  //   (precedingBasicBlock2, (killedRegister3, killedRegister3))  // edge 2
  // ) |                      |
  //   +-- edge              +-- kills

  const llvm::Instruction *inst = &*dst->begin();
  if (llvm::MDNode *edges = inst->getMetadata("liveregister.killed")) {
    for (std::size_t i = 0; i < edges->getNumOperands(); ++i) {
      llvm::Value *edgeValue = edges->getOperand(i);
      if (llvm::MDNode *edge = dyn_cast<llvm::MDNode>(edgeValue)) {
        if (edge->getNumOperands() != 2) {
          llvm::errs() << "MemoryState: liveregister.killed metadata is in "
                       << "wrong shape\n";
        }
        llvm::Value *bbValue = edge->getOperand(0);
        llvm::BasicBlock *bb = dyn_cast<llvm::BasicBlock>(bbValue);
        if (bb != nullptr) {
          if (bb != src) {
            // wrong edge: no evaluation of registers to kill, go to next edge
            continue;
          }
        } else {
          llvm::errs() << "MemoryState: liveregister.killed metadata does not "
                       << "reference valid basic block\n";
        }

        llvm::Value *killsValue = edge->getOperand(1);
        llvm::MDNode *killsNode = dyn_cast<llvm::MDNode>(killsValue);
        for (std::size_t j = 0; j < killsNode->getNumOperands(); ++j) {
          llvm::Value *kill = killsNode->getOperand(j);
          llvm::Instruction *inst = static_cast<llvm::Instruction *>(kill);
          if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
            llvm::errs() << "MemoryState: not live anymore: "
                         << inst->getName() << "\n";
          }
          unregisterLocal(inst);
          clearLocal(inst);
        }

        // correct edge was found, loop can be terminated
        break;
      }
    }
  }

  registerBasicBlock(getKInstruction(dst));
}

KInstruction *MemoryState::getKInstruction(const llvm::BasicBlock* bb) {
  KFunction *kf = executionState.stack.back().kf;
  unsigned entry = kf->basicBlockEntry[const_cast<llvm::BasicBlock *>(bb)];
  return kf->instructions[entry];
}

KInstruction *MemoryState::getKInstruction(const llvm::Instruction* inst) {
  // FIXME: ugly hack
  llvm::BasicBlock *bb = const_cast<llvm::BasicBlock *>(inst->getParent());
  if (bb != nullptr) {
    KFunction *kf = executionState.stack.back().kf;
    if (kf != nullptr) {
      unsigned entry = kf->basicBlockEntry[bb];
      while ((entry + 1) < kf->numInstructions
             && kf->instructions[entry]->inst != inst)
      {
        entry++;
      }
      return kf->instructions[entry];
    }
  }
  return nullptr;
}

ref<Expr> MemoryState::getLocalValue(const KInstruction *kinst) {
  return executionState.stack.back().locals[kinst->dest].value;
}

ref<Expr> MemoryState::getLocalValue(const llvm::Instruction *inst) {
  KInstruction *kinst = getKInstruction(inst);
  if (kinst != nullptr) {
    return getLocalValue(kinst);
  }
  return nullptr;
}

void MemoryState::clearLocal(const KInstruction *kinst) {
  executionState.stack.back().locals[kinst->dest].value = nullptr;
  assert(getLocalValue(kinst).isNull());
}

void MemoryState::clearLocal(const llvm::Instruction *inst) {
  KInstruction *kinst = getKInstruction(inst);
  if (kinst != nullptr) {
    clearLocal(kinst);
  }
  assert(getLocalValue(inst).isNull());
}


bool MemoryState::findLoop() {
  if (libraryFunction.entered || outputFunction.entered) {
    // we do not want to find infinite loops in library or output functions
    return false;
  }

  bool result = trace.findLoop();

  if (DebugInfiniteLoopDetection.isSet(STDERR_TRACE)) {
    if (result) {
      trace.dumpTrace();
    }
  }

  return result;
}

bool MemoryState::enterOutputFunction(llvm::Function *f) {
  if (outputFunction.entered) {
    // we can only enter one output function at a time
    // (we do not need to register additional output functions called by e.g.
    // printf)
    return false;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: entering output function: "
                 << f->getName() << "\n";
  }

  outputFunction.entered = true;
  outputFunction.function = f;

  return true;
}

void MemoryState::leaveOutputFunction() {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: leaving output function: "
                 << outputFunction.function->getName() << "\n";
  }

  outputFunction.entered = false;
  outputFunction.function = nullptr;
}

bool MemoryState::isInOutputFunction(llvm::Function *f) {
  return (outputFunction.entered && f == outputFunction.function);
}

bool MemoryState::enterLibraryFunction(llvm::Function *f,
  ref<ConstantExpr> address, const MemoryObject *mo, const ObjectState *os,
  std::size_t bytes) {
  if (libraryFunction.entered) {
    // we can only enter one library function at a time
    klee_warning_once(f, "already entered a library function");
    return false;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: entering library function: "
                 << f->getName() << "\n";
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
  return (libraryFunction.entered && f == libraryFunction.function);
}

const MemoryObject *MemoryState::getLibraryFunctionMemoryObject() {
  return libraryFunction.mo;
}

void MemoryState::leaveLibraryFunction(const ObjectState *os) {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: leaving library function: "
                 << libraryFunction.function->getName() << "\n";
  }

  libraryFunction.entered = false;
  libraryFunction.function = nullptr;

  registerWrite(libraryFunction.address, *libraryFunction.mo, *os,
    libraryFunction.bytes);
}

void MemoryState::registerPushFrame(const KFunction *kf) {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: PUSHFRAME\n";
  }

  trace.registerEndOfStackFrame(kf,
                                fingerprint.getLocalDelta(),
                                fingerprint.getAllocaDelta(),
                                globalAllocationsInCurrentStackFrame);

  // make locals and arguments "invisible"
  fingerprint.discardLocalDelta();
  // record alloca allocations and changes for this new stack frame separately
  // from those of other stack frames (without removing the latter)
  fingerprint.applyAndResetAllocaDelta();

  // reset stack frame specific information
  globalAllocationsInCurrentStackFrame = false;

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }
}

void MemoryState::registerPopFrame(const llvm::BasicBlock *returningBB,
                                   const llvm::BasicBlock *callerBB) {
  // IMPORTANT: has to be called prior to state.popFrame()

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: POPFRAME\n"
                 << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }

  if (trace.getNumberOfStackFrames() > 0) {
    // Even though the fingerprint delta (that contains registers) is removed in
    // the next step, we have to clear consumed locals within KLEE to be able to
    // determine which variable has already been registered during another call
    // to the function we are currently leaving.
    removeConsumedLocals(returningBB, false);

    MemoryTrace::StackFrameEntry sfe = trace.popFrame();

    // remove locals and arguments of stack frame that is to be left
    fingerprint.discardLocalDelta();
    // set local delta to fingerprint local delta of stack frame that is to be
    // entered to make locals and arguments "visible" again
    fingerprint.setLocalDelta(sfe.fingerprintLocalDelta);

    // remove allocas allocated in stack frame that is to be left
    fingerprint.discardAllocaDelta();
    // initialize alloca delta with previous fingerprint alloca delta which
    // contains information on allocas allocated in the stack frame that is to
    // be entered
    fingerprint.setAllocaDeltaToPreviousValue(sfe.fingerprintAllocaDelta);

    populateLiveRegisters(callerBB);

    globalAllocationsInCurrentStackFrame = sfe.globalAllocation;

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "reapplying local delta: "
                   << fingerprint.getLocalDeltaAsString()
                   << "\nreapplying alloca delta: "
                   << fingerprint.getAllocaDeltaAsString()
                   << "\nGlobal Alloc: " << globalAllocationsInCurrentStackFrame
                   << "\nFingerprint: " << fingerprint.getFingerprintAsString()
                   << "\n";
    }
  } else {
    // no stackframe left to pop

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
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
