#include "AddressSpace.h"
#include "Memory.h"
#include "MemoryState.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Support/SHA1.h"

#include <array>
#include <iomanip>

namespace klee {


void MemoryState::addObjectState(const MemoryObject &mo, const ObjectState &os) {
  util::SHA1 sha1;

  std::array<std::uint8_t, 20> hashDigest;

  uint64_t address = mo.address;
  for(std::size_t i = 0; i < os.size; i++) {
    addAddressToHash(sha1, &address);

    ref<Expr> valExpr = os.read8(i);
    uint8_t value = dyn_cast<ConstantExpr>(valExpr)->getZExtValue(8);
    sha1.update_single(value);

    sha1.store_result(hashDigest.begin(), hashDigest.end());
    xorHash(hashDigest);
    sha1.reset();

    address++;
  }

}


void MemoryState::registerAllocation(const MemoryObject &mo) {
  util::SHA1 sha1;
  std::array<std::uint8_t, 20> hashDigest;

  addAddressToHash(sha1, &mo.address);
  sha1.store_result(hashDigest.begin(), hashDigest.end());

  xorHash(hashDigest);
}

void MemoryState::registerDeallocation(const MemoryObject &mo) {
  registerAllocation(mo);
}

void MemoryState::registerWrite(ExecutionState &state, ref<Expr> base) {
  ObjectPair op;
  bool success = state.addressSpace.resolveOne(dyn_cast<ConstantExpr>(base), op);
  assert(success &&
    "address could not be resolved");

  addObjectPair(op);
}


void MemoryState::addAddressToHash(util::SHA1 &sha1, const uint64_t *address) {
  sha1.update_single(static_cast<std::uint8_t>(*address >> 56));
  sha1.update_single(static_cast<std::uint8_t>(*address >> 48));
  sha1.update_single(static_cast<std::uint8_t>(*address >> 40));
  sha1.update_single(static_cast<std::uint8_t>(*address >> 32));
  sha1.update_single(static_cast<std::uint8_t>(*address >> 24));
  sha1.update_single(static_cast<std::uint8_t>(*address >> 16));
  sha1.update_single(static_cast<std::uint8_t>(*address >>  8));
  sha1.update_single(static_cast<std::uint8_t>(*address >>  0));
}


void MemoryState::xorHash(const std::array<std::uint8_t, 20> &hash) {
  for (std::size_t i = 0; i < 20; ++i) {
    shaBuffer[i] ^= hash[i];
  }
}

}
