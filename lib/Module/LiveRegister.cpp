//===-- LiveRegister.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#if LLVM_VERSION_CODE < LLVM_VERSION(3, 5)
#include "llvm/Support/CFG.h"
#include "llvm/Support/InstIterator.h"
#else
#include "llvm/IR/CFG.h"
#include "llvm/IR/InstIterator.h"
#endif

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/User.h"
#else
#include "llvm/LLVMContext.h"
#include "llvm/User.h"
#endif

#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Metadata.h"

using namespace llvm;

namespace klee {

char LiveRegisterPass::ID = 0;

bool LiveRegisterPass::runOnFunction(Function &F) {
  // Add nop instruction before the first instruction of each basic block
  // to get live registers at the beginning of each basic block (as our analysis
  // propagates backwards, we would otherwise only know thich registers are live
  // after the execution of the first instruction of each basic block.
  // Introduced instructions are removed again at the end of this pass.
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    llvm::ConstantInt *zero =
        llvm::ConstantInt::get(bb.getContext(), llvm::APInt(1, 0, false));
    Instruction *nop = llvm::BinaryOperator::Create(
        llvm::Instruction::BinaryOps::Or, zero, zero);
    bb.getInstList().insert(&*bb.begin(), nop);
  }

  initializeWorklist(F);
  executeWorklistAlgorithm();

  attachAnalysisResultAsMetadata(F);

  // remove nop instructions that were added in the initialization phase
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    Instruction &nop = *bb.begin();
    nop.eraseFromParent();
  }

  // function was modified (metadata was added)
  return true;
}

void LiveRegisterPass::initializeWorklist(Function &F) {
  // clean up from previous runs
  worklist.clear();
  instructions.clear();

  // generate gen and kill sets for each instruction
  generateInstructionInfo(F);

  for (inst_iterator it = inst_begin(F), e = inst_end(F); it != e; ++it) {
    addPredecessors(worklist, &*it);
  }
}

void LiveRegisterPass::executeWorklistAlgorithm() {
  while (!worklist.empty()) {
    const Instruction *i = worklist.back().first;
    const Instruction *pred = worklist.back().second;
    worklist.pop_back();

    InstructionInfo &iII = getInstructionInfo(i);
    InstructionInfo &predII = getInstructionInfo(pred);

    valueset_t liveUpdated = transition(i, iII.live);
    if (!subsetEquals(liveUpdated, predII.live)) {
      predII.live = setUnion(predII.live, liveUpdated);
      addPredecessors(worklist, pred);
    }
  }
}

void LiveRegisterPass::attachAnalysisResultAsMetadata(Function &F) {
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    LLVMContext &ctx = bb.getContext();

    // first real instruction after nop
    Instruction *first = &*(std::next(bb.begin()));
    // use live register set of nop instruction as instruction info only
    // reflects registers that are live *after* the execution of the associated
    // instruction
    valueset_t &firstLive = getInstructionInfo(&*bb.begin()).live;

    Instruction *term = bb.getTerminator();
    valueset_t &termLive = getInstructionInfo(term).live;

    // attach to terminator instruction: live registers, i.e. registers that are
    // live at the end of the annotated basic block
    // Example:
    // (liveRegister1, liveRegister2, liveRegister3)
    std::vector<Value *> liveVec(termLive.begin(), termLive.end());
    term->setMetadata("liveregister.live", MDNode::get(ctx, liveVec));

    // attach to terminator instruction: "consumed" registers, i.e. registers
    // that are read for the last time within the basic block
    // Example:
    // (consumedRegister1, consumedRegister2, consumedRegister3)
    valueset_t consumed = setMinus(firstLive, termLive);
    std::vector<Value *> consumedVec(consumed.begin(), consumed.end());
    term->setMetadata("liveregister.consumed", MDNode::get(ctx, consumedVec));

    // attach to first instruction: immediately killed registers for each
    // incoming basic block, i.e. registers that are only used by specific basic
    // blocks but not the annotated one
    // WARNING: we specifically exclude registers that are consumed during the
    //          annotated basic block from this set
    // Example:
    // (
    //   (precedingBasicBlock1, (killedRegister1, killedRegister2)),
    //   (precedingBasicBlock2, (killedRegister3, killedRegister3))
    // )
    std::vector<Value *> k;
    for (auto it = pred_begin(&bb), e = pred_end(&bb); it != e; ++it) {
      BasicBlock *pred = *it;
      if (Instruction *predTerm = pred->getTerminator()) {
        valueset_t &predLive = getInstructionInfo(predTerm).live;
        valueset_t killed = setMinus(predLive, firstLive);
        // exclude registers that are consumed during the annotated basic block
        killed = setMinus(killed, consumed);

        std::vector<Value *> killedVec(killed.begin(), killed.end());
        std::vector<Value *> tuple = {pred, MDNode::get(ctx, killedVec)};
        k.push_back(MDNode::get(ctx, tuple));
      }
    }
    first->setMetadata("liveregister.killed", MDNode::get(ctx, k));
  }
}

void LiveRegisterPass::generateInstructionInfo(Function &F) {
  // iterate over all basic blocks
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    Instruction *previ = bb.begin();
    // iterate over all instructions within a basic block
    for (BasicBlock::iterator it = bb.begin(), e = bb.end(); it != e; ++it) {
      Instruction *i = &*it;
      instructionIndex[i] = instructions.size();
      instructions.emplace_back();
      InstructionInfo &ii = instructions.back();

      // generate predecessorEdges
      if (i == bb.begin()) {
        // first instruction in basic block: insert terminator instructions of
        // all preceding basic blocks as predecessors
        for (auto it = pred_begin(&bb), e = pred_end(&bb); it != e; ++it) {
          BasicBlock *pred = *it;
          if (Instruction *predTerm = pred->getTerminator()) {
            ii.predecessorEdges.emplace_back(std::make_pair(i, predTerm));
          }
        }
      } else {
        // all further instructions: insert previous instruction as predecessor
        ii.predecessorEdges.emplace_back(std::make_pair(i, previ));
      }

      // function arguments are regarded as live and are thus ignored when
      // generating gen and kill sets

      // generate kill sets
      if (i->getNumUses() != 0) {
        ii.kill.insert(i);
      }

      // generate gen sets
      if (i->getNumOperands() != 0) {
        for (auto use = i->op_begin(), e = i->op_end(); use != e; ++use) {
          if (Instruction *op = dyn_cast<Instruction>(use->get())) {
            ii.gen.insert(op);
          }
        }
      }

      previ = i;
    }
  }
}

void LiveRegisterPass::addPredecessors(std::vector<edge_t> &worklist,
                                       const Instruction *i) {
  std::vector<edge_t> &predEdges = getInstructionInfo(i).predecessorEdges;
  worklist.insert(worklist.end(), predEdges.begin(), predEdges.end());
}

LiveRegisterPass::valueset_t
LiveRegisterPass::transition(const Instruction *i, const valueset_t &set) {
  InstructionInfo &ii = getInstructionInfo(i);
  valueset_t result = set;

  result = setMinus(result, ii.kill);
  result = setUnion(result, ii.gen);
  return result;
}

template <typename T>
bool LiveRegisterPass::subsetEquals(const std::unordered_set<T> &subset,
                                    const std::unordered_set<T> &set) {
  if (subset.size() == 0) {
    return true;
  }
  for (auto &i : subset) {
    typename std::unordered_set<T>::const_iterator pos = set.find(i);
    if (pos == set.end()) {
      return false;
    }
  }
  return true;
}

template <typename T>
std::unordered_set<T>
LiveRegisterPass::setMinus(const std::unordered_set<T> &set,
                           const std::unordered_set<T> &minus) {
  std::unordered_set<T> result = set;
  for (auto &i : minus) {
    typename std::unordered_set<T>::iterator pos = result.find(i);
    if (pos != result.end()) {
      result.erase(pos);
    }
  }
  return result;
}

template <typename T>
std::unordered_set<T>
LiveRegisterPass::setUnion(const std::unordered_set<T> &set1,
                           const std::unordered_set<T> &set2) {
  std::unordered_set<T> result = set1;
  result.insert(set2.begin(), set2.end());
  return result;
}
}