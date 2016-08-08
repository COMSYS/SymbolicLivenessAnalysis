#include "AddressSpace.h"
#include "Memory.h"
#include "MemoryState.h"

#include "klee/ExecutionState.h"

#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>

#define MEMORYSTATE_DEBUG 1

namespace klee {

void MemoryState::printHash() {
  std::cout << "SHA1: " << Sha1String(shaBuffer) << std::endl;
}

void MemoryState::registerAllocation(const MemoryObject &mo) {
  util::SHA1 sha1;
  std::array<std::uint8_t, 20> hashDigest;

  addUint64ToHash(sha1, &mo.address);
  sha1.store_result(hashDigest.begin(), hashDigest.end());
  xorHash(hashDigest);

  #ifdef MEMORYSTATE_DEBUG
    std::cout << "MemoryState: processing (de)allocation at address " << mo.address;
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

  for(std::uint64_t offset = 0; offset < os.size; offset++) {
    // add base address to sha1 hash
    if(ConstantExpr *constant = dyn_cast<ConstantExpr>(base)) {
      // concrete address
      std::uint64_t address = constant->getZExtValue(64);
      addUint64ToHash(sha1, &address);
    } else {
      // symbolic address
      addExprToHash(sha1, base);
    }

    // add current offset to hash
    addUint64ToHash(sha1, &offset);

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
      addExprToHash(sha1, valExpr);
      #ifdef MEMORYSTATE_DEBUG
        std::cout << ExprString(valExpr);
      #endif
    }

    // compute sha1 hash and xor to existing hash
    sha1.store_result(hashDigest.begin(), hashDigest.end());
    xorHash(hashDigest);

    #ifdef MEMORYSTATE_DEBUG
      std::cout << " [sha1: " << Sha1String(hashDigest) << "]" << std::endl;
    #endif

    sha1.reset();
  }
}

void MemoryState::registerConstraint(ref<Expr> condition) {
  util::SHA1 sha1;
  std::array<std::uint8_t, 20> hashDigest;

  addExprToHash(sha1, condition);
  sha1.store_result(hashDigest.begin(), hashDigest.end());
  xorHash(hashDigest);

  #ifdef MEMORYSTATE_DEBUG
    std::cout << "MemoryState: adding new constraint: " << ExprString(condition);
    std::cout << " [sha1: " << Sha1String(hashDigest) << "]" << std::endl;
  #endif
}

void MemoryState::addUint64ToHash(util::SHA1 &sha1, const std::uint64_t *value) {
  sha1.update_single(static_cast<std::uint8_t>(*value >> 56));
  sha1.update_single(static_cast<std::uint8_t>(*value >> 48));
  sha1.update_single(static_cast<std::uint8_t>(*value >> 40));
  sha1.update_single(static_cast<std::uint8_t>(*value >> 32));
  sha1.update_single(static_cast<std::uint8_t>(*value >> 24));
  sha1.update_single(static_cast<std::uint8_t>(*value >> 16));
  sha1.update_single(static_cast<std::uint8_t>(*value >>  8));
  sha1.update_single(static_cast<std::uint8_t>(*value >>  0));
}

void MemoryState::addExprToHash(util::SHA1 &sha1, ref<Expr> expr) {
  std::string str;
  llvm::raw_string_ostream ostream(str);
  expr->print(ostream);
  ostream.flush();
  sha1.update_range(str.begin(), str.end());
}

void MemoryState::xorHash(const std::array<std::uint8_t, 20> &hash) {
  for (std::size_t i = 0; i < 20; ++i) {
    shaBuffer[i] ^= hash[i];
  }
}

std::string MemoryState::ExprString(ref<Expr> expr) {
  std::string result;
  llvm::raw_string_ostream ostream(result);
  expr->print(ostream);
  ostream.flush();
  return result;
}

std::string MemoryState::Sha1String(std::array<std::uint8_t, 20> &buffer) {
  std::stringstream result;
  for (std::array<std::uint8_t, 20>::iterator iter = buffer.begin(); iter != buffer.end(); ++iter) {
      result << std::hex << std::setfill('0') << std::setw(2);
      result << static_cast<unsigned int>(*iter);
  }
  return result.str();
}

}
