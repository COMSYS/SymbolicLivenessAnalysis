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

#include <algorithm>

using namespace llvm;

namespace klee {

char LiveRegisterPass::ID = 0;

bool LiveRegisterPass::runOnFunction(Function &F) {
  // Add nop instruction before the first instruction of each basic block
  // to get live registers at the beginning of each basic block (as our analysis
  // propagates backwards, we would otherwise only know which registers are live
  // after the execution of the first instruction of each basic block).
  // Introduced instructions are removed again at the end of this pass.
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    LLVMContext &ctx = bb.getContext();
    bb.getInstList().insert(&*bb.begin(), createNopInstruction(ctx));
  }

  initializeWorklist(F);
  executeWorklistAlgorithm();
  propagatePhiUseToLiveSet(F);

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
      predII.live.insert(liveUpdated.begin(), liveUpdated.end());
      addPredecessors(worklist, pred);
    }
  }
}

void LiveRegisterPass::propagatePhiUseToLiveSet(Function &F) {
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    Instruction *term = bb.getTerminator();
    valueset_t &termLive = getInstructionInfo(term).live;
    valueset_t &termGen = getInstructionInfo(term).gen;

    // If a register is only used by PHI nodes in all following blocks, it is
    // included in the gen set of the terminator instruction but was not yet
    // propagated into its live set. Here, we simply insert all values that are
    // used by PHI nodes into the terminator instruction's live set.
    for (Value *value : termGen) {
      for (auto i = value->use_begin(), e = value->use_end(); i != e; ++i) {
        if (Instruction *inst = dyn_cast<Instruction>(*i)) {
          if (inst->getOpcode() == Instruction::PHI) {
            // found usage of value in gen set by PHI node
            termLive.insert(value);
            // we do only need to add each value once to the live set
            break;
          }
        }
      }
    }
  }
}

void LiveRegisterPass::attachAnalysisResultAsMetadata(Function &F) {
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    LLVMContext &ctx = bb.getContext();

    // use live register set of nop instruction since instruction info only
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

    // attach to terminator instruction: immediately killed registers for each
    // succeeding basic block, i.e. registers that are only used by specific
    // basic blocks but not the succeeding one
    // WARNING: we specifically exclude registers that are consumed during the
    //          execution of the succeding basic block from this set
    // Example:
    // (
    //   (succeedingBasicBlock1, (killedRegister1, killedRegister2)),
    //   (succeedingBasicBlock2, (killedRegister3, killedRegister3))
    // )
    std::vector<Value *> k;
    for (auto it = succ_begin(&bb), e = succ_end(&bb); it != e; ++it) {
      BasicBlock *succ = *it;
      if (Instruction *succFirst = &*succ->begin()) {
        valueset_t &succFirstLive = getInstructionInfo(succFirst).live;
        valueset_t killed = setMinus(termLive, succFirstLive);

        // exclude registers that are consumed during the succeeding basic block
        Instruction *succTerm = succ->getTerminator();
        valueset_t &succTermLive = getInstructionInfo(succTerm).live;
        valueset_t succConsumed = setMinus(succFirstLive, succTermLive);
        killed = setMinus(killed, succConsumed);

        // exclude registers that are being written to by PHI nodes in
        // succeeding basic block
        for (auto it = std::next(succ->begin()), e = succ->end(); it != e; ++it)
        {
          Instruction &i = *it;
          const Instruction *lastPHI = &i;
          if (i.getOpcode() == Instruction::PHI) {
            Value *phiValue = cast<Value>(&i);

            // last PHI node in current BasicBlock
            // Note: We cannot use getFirstNonPHI() here because of the NOP.
            while (lastPHI->getNextNode()->getOpcode() == Instruction::PHI) {
                lastPHI = lastPHI->getNextNode();
            }

            // values live after the evaluation of last PHI node
            valueset_t &phiLive = getInstructionInfo(lastPHI).live;

            if (phiLive.count(phiValue) > 0) {
                killed.erase(phiValue);
            }
          } else {
            // http://releases.llvm.org/3.4.2/docs/LangRef.html#phi-instruction
            // "There must be no non-phi instructions between the start of a
            //  basic block and the PHI instructions: i.e. PHI instructions must
            //  be first in a basic block."
            // Thus, we can abort as soon as we encounter a non-PHI instruction
            // (This is only valid here, because in this for-loop, we skip the
            // nop instruction inserted at the begining of each basic block)
            break;
          }
        }

        std::vector<Value *> killedVec(killed.begin(), killed.end());
        std::vector<Value *> tuple = {succ, MDNode::get(ctx, killedVec)};
        k.push_back(MDNode::get(ctx, tuple));
      }
    }
    term->setMetadata("liveregister.killed", MDNode::get(ctx, k));
  }
}

void LiveRegisterPass::generateInstructionInfo(Function &F) {
  // iterate over all basic blocks
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    // iterate over all instructions within a basic block
    for (BasicBlock::iterator it = bb.begin(), e = bb.end(); it != e; ++it) {
      Instruction *i = &*it;
      instructionIndex[i] = instructions.size();
      instructions.emplace_back();
    }
  }

  // In the following loop, we assume that each instruction within the function
  // has a corresponding InstructionInfo entry, which is why we need to iterate
  // over all instructions twice.

  // iterate over all basic blocks
  for (Function::iterator it = F.begin(), e = F.end(); it != e; ++it) {
    BasicBlock &bb = *it;
    Instruction *previ = bb.begin();
    // iterate over all instructions within a basic block
    for (BasicBlock::iterator it = bb.begin(), e = bb.end(); it != e; ++it) {
      Instruction *i = &*it;
      InstructionInfo &ii = getInstructionInfo(i);

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

      // parameters of functions are regarded as live and are thus ignored when
      // generating gen and kill sets

      // generate kill sets
      if (i->getNumUses() != 0) {
        ii.kill.insert(i);
      }

      // generate gen sets
      if (i->getOpcode() == Instruction::PHI) {
        // PHI nodes: attach operand to gen set of the incoming basic
        // block's terminator instruction because operands of PHI nodes
        // are not in general live at the start of the basic block that
        // contain the corresponding PHI node
        PHINode *phi = cast<PHINode>(i);

        // iterate over all incoming basic blocks
        for (auto ib = phi->block_begin(), e = phi->block_end(); ib != e; ++ib)
        {
          Value *value = phi->getIncomingValueForBlock(*ib);
          if (isa<Instruction>(value)) {
            Instruction *incomingTerm = (*ib)->getTerminator();
            InstructionInfo &incomingII = getInstructionInfo(incomingTerm);
            incomingII.gen.insert(value);
          }
        }
      } else if (i->getNumOperands() != 0) {
        // iterate over all operands (uses) of the current instruction
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
  result.insert(ii.gen.begin(), ii.gen.end());
  return result;
}

Instruction *LiveRegisterPass::createNopInstruction(LLVMContext &ctx) const {
  ConstantInt *zero = ConstantInt::get(ctx, APInt(1, 0, false));
  return BinaryOperator::Create(Instruction::BinaryOps::Or, zero, zero);
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


}
