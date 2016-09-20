#include "AddressSpace.h"
#include "Memory.h"
#include "MemoryState.h"

#include "klee/ExecutionState.h"

#include <array>
#include <cmath>

#ifdef MEMORYSTATE_DEBUG
#include <iomanip>
#include <iostream>
#include <sstream>
#endif

namespace klee {

void MemoryState::registerExternalFunctionCall() {
  trace.clear();
}

void MemoryState::registerAllocation(const MemoryObject &mo) {
  util::SHA1 sha1;
  std::array<std::uint8_t, 20> hashDigest;

  addUint64ToHash(sha1, mo.address);
  addUint64ToHash(sha1, mo.size);

  sha1.store_result(hashDigest.begin(), hashDigest.end());
  xorStateHash(hashDigest);

  #ifdef MEMORYSTATE_DEBUG
    std::cout << "MemoryState: processing (de)allocation at address " << std::dec << mo.address << " of size " << mo.size;
    std::cout << " [sha1: " << Sha1String(hashDigest) << "]" << std::endl;
  #endif
}

void MemoryState::registerWrite(ref<Expr> base, const MemoryObject &mo, const ObjectState &os) {
  util::SHA1 sha1;
  std::array<std::uint8_t, 20> hashDigest;

  #ifdef MEMORYSTATE_DEBUG
    std::uint8_t offsetWidth = static_cast<std::uint8_t>(ceil(log10(os.size)));
    std::cout << "MemoryState: processing ObjectState at base address " << ExprString(base) << std::endl;
  #endif

  if(!allocasInCurrentStackFrame)
    allocasInCurrentStackFrame = true;

  for(std::uint64_t offset = 0; offset < os.size; offset++) {
    // add base address to sha1 hash
    if(ConstantExpr *constant = dyn_cast<ConstantExpr>(base)) {
      // concrete address
      assert(constant->getWidth() <= 64 &&
             "address greater than 64 bit!");
      std::uint64_t address = constant->getZExtValue(64);
      addUint64ToHash(sha1, address);
    } else {
      // symbolic address
      addExprStringToHash(sha1, base);
    }

    // add current offset to hash
    addUint64ToHash(sha1, offset);

    #ifdef MEMORYSTATE_DEBUG
      std::cout << "[+" << std::setfill(' ') << std::setw(offsetWidth) << std::dec << offset << "] ";
    #endif

    // add value of byte at offset to hash
    ref<Expr> valExpr = os.read8(offset);
    if(ConstantExpr *constant = dyn_cast<ConstantExpr>(valExpr)) {
      // concrete value
      std::uint8_t value = constant->getZExtValue(8);
      sha1.update_single(value);
      #ifdef MEMORYSTATE_DEBUG
        std::cout << "0x" <<std::uppercase << std::hex << std::setfill('0') << std::setw(2) << (int) value;
      #endif
    } else {
      // symbolic value
      addExprStringToHash(sha1, valExpr);
      #ifdef MEMORYSTATE_DEBUG
        std::cout << ExprString(valExpr);
      #endif
    }

    // compute sha1 hash and xor to existing hash
    sha1.store_result(hashDigest.begin(), hashDigest.end());
    xorStateHash(hashDigest);

    #ifdef MEMORYSTATE_DEBUG
      std::cout << " [sha1: " << Sha1String(hashDigest) << "]" << std::endl;
    #endif

    sha1.reset();
  }
}

void MemoryState::registerConstraint(ref<Expr> condition) {
  util::SHA1 sha1;
  std::array<std::uint8_t, 20> hashDigest;

  addExprStringToHash(sha1, condition);
  sha1.store_result(hashDigest.begin(), hashDigest.end());
  xorStateHash(hashDigest);

  #ifdef MEMORYSTATE_DEBUG
    std::cout << "MemoryState: adding new constraint: " << ExprString(condition);
    std::cout << " [sha1: " << Sha1String(hashDigest) << "]" << std::endl;
  #endif
}

void MemoryState::registerLocal(const KInstruction *target, ref<Expr> value) {
  util::SHA1 sha1;
  std::array<std::uint8_t, 20> hashDigest;

  addUint64ToHash(sha1, reinterpret_cast<std::intptr_t>(target));

  if(ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    addConstantExprToHash(sha1, *constant);
  } else {
    // symbolic value
    addExprStringToHash(sha1, value);
  }

  sha1.store_result(hashDigest.begin(), hashDigest.end());
  xorStateHash(hashDigest);
  xorStackFrameHash(hashDigest);

  #ifdef MEMORYSTATE_DEBUG
    std::cout << "MemoryState: adding local"
        << " to instruction " << reinterpret_cast<std::intptr_t>(target)
        << ": " << ExprString(value) << std::endl;
    std::cout << " [sha1: " << Sha1String(hashDigest) << "]" << std::endl;
  #endif
}

void MemoryState::registerArgument(const KFunction *kf, unsigned index, ref<Expr> value) {
  util::SHA1 sha1;
  std::array<std::uint8_t, 20> hashDigest;

  addUint64ToHash(sha1, reinterpret_cast<std::intptr_t>(kf));

  if(ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    addConstantExprToHash(sha1, *constant);
  } else {
    // symbolic value
    addExprStringToHash(sha1, value);
  }

  sha1.store_result(hashDigest.begin(), hashDigest.end());
  xorStateHash(hashDigest);
  xorStackFrameHash(hashDigest);

  #ifdef MEMORYSTATE_DEBUG
    std::cout << "MemoryState: adding argument " << index
        << " to function " << reinterpret_cast<std::intptr_t>(kf)
        << ": " << ExprString(value) << std::endl;
    std::cout << " [sha1: " << Sha1String(hashDigest) << "]" << std::endl;
  #endif
}


void MemoryState::registerBasicBlock(const KInstruction *inst) {
  #ifdef MEMORYSTATE_DEBUG
  std::cout << "MemoryState: BASICBLOCK\n";
  #endif

  trace.registerBasicBlock(inst, stateHash);
}

bool MemoryState::findLoop() {
  bool result = trace.findLoop();

  #ifdef MEMORYTRACE_DEBUG
  if(result) {
    trace.debugStack();
  }
  #endif

  return result;
}

void MemoryState::registerPushFrame() {
  #ifdef MEMORYSTATE_DEBUG
  std::cout << "MemoryState: PUSHFRAME\n";
  #endif

  trace.registerEndOfStackFrame(stackFrameHash, allocasInCurrentStackFrame);

  // remove difference from stateHash
  // to make locals and parameters "invisible"
  xorStateHash(stackFrameHash);

  // reset stack frame specific information
  stackFrameHash = {};
  allocasInCurrentStackFrame = false;
}

void MemoryState::registerPopFrame() {
  #ifdef MEMORYSTATE_DEBUG
  std::cout << "MemoryState: POPFRAME\n";
  #endif

  // apply old difference to stateHash
  // to make locals and parameters "visible" again
  std::array<std::uint8_t, 20> hashDifference = trace.popFrame();
  xorStateHash(hashDifference);
}

void MemoryState::addUint64ToHash(util::SHA1 &sha1, const std::uint64_t value) {
  sha1.update_single(static_cast<std::uint8_t>(value >> 56));
  sha1.update_single(static_cast<std::uint8_t>(value >> 48));
  sha1.update_single(static_cast<std::uint8_t>(value >> 40));
  sha1.update_single(static_cast<std::uint8_t>(value >> 32));
  sha1.update_single(static_cast<std::uint8_t>(value >> 24));
  sha1.update_single(static_cast<std::uint8_t>(value >> 16));
  sha1.update_single(static_cast<std::uint8_t>(value >>  8));
  sha1.update_single(static_cast<std::uint8_t>(value >>  0));
}

void MemoryState::addConstantExprToHash(util::SHA1 &sha1, const ConstantExpr &expr) {
  if(expr.getWidth() <= 64) {
    std::uint64_t constantValue = expr.getZExtValue(64);
    addUint64ToHash(sha1, constantValue);
  } else {
    const llvm::APInt& value = expr.getAPValue();
    for(std::size_t i = 0; i != value.getNumWords(); i++) {
      std::uint64_t word = value.getRawData()[i];
      addUint64ToHash(sha1, word);
    }
  }
}

void MemoryState::addExprStringToHash(util::SHA1 &sha1, ref<Expr> expr) {
  std::string str;
  llvm::raw_string_ostream ostream(str);
  expr->print(ostream);
  ostream.flush();
  sha1.update_range(str.begin(), str.end());
}

void MemoryState::xorStateHash(const std::array<std::uint8_t, 20> &hash) {
  for (std::size_t i = 0; i < 20; ++i) {
    stateHash[i] ^= hash[i];
  }
}

void MemoryState::xorStackFrameHash(const std::array<std::uint8_t, 20> &hash) {
  for (std::size_t i = 0; i < 20; ++i) {
    stackFrameHash[i] ^= hash[i];
  }
}

std::string MemoryState::ExprString(ref<Expr> expr) {
  std::string result;
  llvm::raw_string_ostream ostream(result);
  expr->print(ostream);
  ostream.flush();
  return result;
}

#ifdef MEMORYSTATE_DEBUG
std::string MemoryState::Sha1String(const std::array<std::uint8_t, 20> &buffer) {
  std::stringstream result;
  for (std::array<std::uint8_t, 20>::const_iterator iter = buffer.cbegin(); iter != buffer.cend(); ++iter) {
      result << std::hex << std::setfill('0') << std::setw(2);
      result << static_cast<unsigned int>(*iter);
  }
  return result.str();
}
#endif

}
