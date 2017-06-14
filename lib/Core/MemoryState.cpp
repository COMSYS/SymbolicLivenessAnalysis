#include "AddressSpace.h"
#include "DebugInfiniteLoopDetection.h"
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
#include "llvm/IR/Metadata.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace klee {

void MemoryState::registerExternalFunctionCall() {
  if (outputFunction.entered) {
    return;
  }

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

  if (mo.isLocal) {
    fingerprint.applyToFingerprintAndDelta();
  } else {
    fingerprint.applyToFingerprint();
  }

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: processing "
                 << (mo.isLocal ? "local " : "global ")
                 << "(de)allocation at address "
                 << mo.address << " of size " << mo.size
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerWrite(ref<Expr> address, const MemoryObject &mo,
                                const ObjectState &os, std::size_t bytes) {
  if (libraryFunction.entered || outputFunction.entered) {
    return;
  }

  ref<ConstantExpr> base = mo.getBaseExpr();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: processing "
                 << (mo.isLocal ? "local " : "global ")
                 << "ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  if (!globalAllocationsInCurrentStackFrame && !mo.isLocal)
    globalAllocationsInCurrentStackFrame = true;

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

    if (mo.isLocal) {
      fingerprint.applyToFingerprintAndDelta();
    } else {
      fingerprint.applyToFingerprint();
    }

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      if (i % 10 == 9) {
        llvm::errs() << "\n";
      } else {
        llvm::errs() << " ";
      }
    }
  }
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
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

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
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

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
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

  fingerprint.applyToFingerprintAndDelta();
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

  fingerprint.applyToFingerprintAndDelta();

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: adding argument " << index << " to function "
                 << reinterpret_cast<std::intptr_t>(kf) << ": "
                 << ExprString(value) << "\n"
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}


void MemoryState::populateLiveRegisters(const llvm::BasicBlock *bb) {
  if (basicBlockInfo.bb != bb) {
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
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: BASICBLOCK [fingerprint: "
                 << fingerprint.getFingerprintAsString() << "]\n";
  }

  llvm::BasicBlock *bb = inst->inst->getParent();
  populateLiveRegisters(bb);
  trace.registerBasicBlock(inst, fingerprint.getFingerprint());
}

void MemoryState::removeConsumedLocals(const ExecutionState *state,
                                          llvm::BasicBlock *bb,
                                          bool unregister) {
    const llvm::Instruction *ti = bb->getTerminator();
    llvm::MDNode *consumedRegisters = ti->getMetadata("liveregister.consumed");
    if (consumedRegisters != nullptr) {
      for (std::size_t i = 0; i < consumedRegisters->getNumOperands(); ++i) {
        llvm::Value *consumed = consumedRegisters->getOperand(i);
        llvm::Instruction *inst = static_cast<llvm::Instruction *>(consumed);
        if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
          llvm::errs() << "MemoryState: consumed by previous basic block: "
                       << inst->getName() << "\n";
        }
        if (unregister) {
          // remove local from delta
          unregisterLocal(state, inst);
        }
        // set local within KLEE to zero to mark them as dead
        clearLocal(state, inst);
      }
    }
}

void MemoryState::registerBasicBlock(const ExecutionState *state,
                                     llvm::BasicBlock *dst,
                                     llvm::BasicBlock *src) {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "registerBasicBlock " << dst->getName()
                 << " (coming from " << src->getName() << ")\n";
  }

  removeConsumedLocals(state, src);

  populateLiveRegisters(dst);

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
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
          if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
            llvm::errs() << "MemoryState: not live anymore: "
                         << inst->getName() << "\n";
          }
          unregisterLocal(state, inst);
          clearLocal(state, inst);
        }

        // correct edge was found, loop can be terminated
        break;
      }
    }
  }

  registerBasicBlock(getKInstruction(state, dst));
}

KInstruction *MemoryState::getKInstruction(const ExecutionState *state,
                                           const llvm::BasicBlock* bb)
{
  KFunction *kf = state->stack.back().kf;
  unsigned entry = kf->basicBlockEntry[const_cast<llvm::BasicBlock *>(bb)];
  return kf->instructions[entry];
}

KInstruction *MemoryState::getKInstruction(const ExecutionState *state,
                                           const llvm::Instruction* inst)
{
  // FIXME: ugly hack
  llvm::BasicBlock *bb = const_cast<llvm::BasicBlock *>(inst->getParent());
  if (bb != nullptr) {
    KFunction *kf = state->stack.back().kf;
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

ref<Expr> MemoryState::getLocalValue(const ExecutionState *state,
                                const KInstruction *kinst)
{
  return state->stack.back().locals[kinst->dest].value;
}

ref<Expr> MemoryState::getLocalValue(const ExecutionState *state,
                                const llvm::Instruction *inst)
{
  KInstruction *kinst = getKInstruction(state, inst);
  if (kinst != nullptr) {
    return getLocalValue(state, kinst);
  }
  return nullptr;
}

void MemoryState::clearLocal(const ExecutionState *state,
                             const KInstruction *kinst)
{
  state->stack.back().locals[kinst->dest].value = nullptr;
  assert(getLocalValue(state, kinst).isNull());
}

void MemoryState::clearLocal(const ExecutionState *state,
                             const llvm::Instruction *inst)
{
  KInstruction *kinst = getKInstruction(state, inst);
  if (kinst != nullptr) {
    clearLocal(state, kinst);
  }
  assert(getLocalValue(state, inst).isNull());
}


bool MemoryState::findLoop() {
  if (libraryFunction.entered || outputFunction.entered) {
    // we do not want to find infinite loops in library or output functions
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

bool MemoryState::enterOutputFunction(llvm::Function *f) {
  if (outputFunction.entered) {
    // we can only enter one output function at a time
    // (we do not need to register additional output functions called by e.g.
    // printf)
    return false;
  }

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: entering output function: "
                 << f->getName() << "\n";
  }

  outputFunction.entered = true;
  outputFunction.function = f;

  return true;
}

void MemoryState::leaveOutputFunction() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
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

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
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
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: leaving library function: "
                 << libraryFunction.function->getName() << "\n";
  }

  libraryFunction.entered = false;
  libraryFunction.function = nullptr;

  registerWrite(libraryFunction.address, *libraryFunction.mo, *os,
    libraryFunction.bytes);
}

void MemoryState::registerPushFrame() {
  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: PUSHFRAME\n";
  }

  trace.registerEndOfStackFrame(fingerprint.getDelta(),
                                globalAllocationsInCurrentStackFrame);

  // make locals and arguments "invisible"
  fingerprint.removeDelta();

  // reset stack frame specific information
  globalAllocationsInCurrentStackFrame = false;

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }
}

void MemoryState::registerPopFrame(const ExecutionState *state,
                                   KInstruction *ki) {
  // has to be called prior to state.popFrame()

  if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
    llvm::errs() << "MemoryState: POPFRAME\n"
                 << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }

  if (trace.getNumberOfStackFrames() > 0) {
    // Even though the fingerprint delta (that contains registers) is removed in
    // the next step, we have to clear consumed locals within KLEE to be able to
    // determine which variable has already been registered during another call
    // to the function we are currently leaving.
    removeConsumedLocals(state, ki->inst->getParent(), false);

    // remove delta (locals and arguments) of stack frame that is to be left
    fingerprint.removeDelta();

    // make locals and arguments "visible" again by
    // applying delta of stack frame that is to be entered
    auto previousFrame = trace.popFrame();
    fingerprint.applyDelta(previousFrame.first);

    globalAllocationsInCurrentStackFrame = previousFrame.second;

    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "reapplying delta: " << fingerprint.getDeltaAsString()
                   << "\nGlobal Alloc: " << globalAllocationsInCurrentStackFrame
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
