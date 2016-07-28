//===-- SHA1.h --------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// Copyright (C) 2016, Daniel Schemmel. All rights reserved.
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

namespace klee {
namespace util {
struct SHA1 {
  static const ::std::size_t hashsize = 160;
  static const ::std::size_t blocksize = 16 * 32;

private:
  ::std::uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  ::std::uint32_t buffer[16];
  ::std::uint64_t count = 0;

  void f() {
    ::std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

    #define ROTL32(x, v) ((x << v) | (x >> (32 - v)))
    #define sha1_schedule(i)                                                                \
      buffer[i] ^= buffer[(i + 13) % 16] ^ buffer[(i + 8) % 16] ^ buffer[(i + 2) % 16];     \
      buffer[i] = ROTL32(buffer[i], 1);
    #define sha1_step(a, b, c, d, e, f, k, w)                                               \
      e = ROTL32(a, 5) + (f(b, c, d)) + e + k + w;                                          \
      b = ROTL32(b, 30);
    #define sha1_step_exp1(b, c, d) ((b & c) | (~b & d))
    #define sha1_step_exp2(b, c, d) (b ^ c ^ d)
    #define sha1_step_exp3(b, c, d) ((b & c) | (b & d) | (c & d))


    sha1_step(a,b,c,d,e, sha1_step_exp1, 0x5A827999u, buffer[ 0]) sha1_schedule( 0)
    sha1_step(e,a,b,c,d, sha1_step_exp1, 0x5A827999u, buffer[ 1]) sha1_schedule( 1)
    sha1_step(d,e,a,b,c, sha1_step_exp1, 0x5A827999u, buffer[ 2]) sha1_schedule( 2)
    sha1_step(c,d,e,a,b, sha1_step_exp1, 0x5A827999u, buffer[ 3]) sha1_schedule( 3)
    sha1_step(b,c,d,e,a, sha1_step_exp1, 0x5A827999u, buffer[ 4]) sha1_schedule( 4)
    sha1_step(a,b,c,d,e, sha1_step_exp1, 0x5A827999u, buffer[ 5]) sha1_schedule( 5)
    sha1_step(e,a,b,c,d, sha1_step_exp1, 0x5A827999u, buffer[ 6]) sha1_schedule( 6)
    sha1_step(d,e,a,b,c, sha1_step_exp1, 0x5A827999u, buffer[ 7]) sha1_schedule( 7)
    sha1_step(c,d,e,a,b, sha1_step_exp1, 0x5A827999u, buffer[ 8]) sha1_schedule( 8)
    sha1_step(b,c,d,e,a, sha1_step_exp1, 0x5A827999u, buffer[ 9]) sha1_schedule( 9)
    sha1_step(a,b,c,d,e, sha1_step_exp1, 0x5A827999u, buffer[10]) sha1_schedule(10)
    sha1_step(e,a,b,c,d, sha1_step_exp1, 0x5A827999u, buffer[11]) sha1_schedule(11)
    sha1_step(d,e,a,b,c, sha1_step_exp1, 0x5A827999u, buffer[12]) sha1_schedule(12)
    sha1_step(c,d,e,a,b, sha1_step_exp1, 0x5A827999u, buffer[13]) sha1_schedule(13)
    sha1_step(b,c,d,e,a, sha1_step_exp1, 0x5A827999u, buffer[14]) sha1_schedule(14)
    sha1_step(a,b,c,d,e, sha1_step_exp1, 0x5A827999u, buffer[15]) sha1_schedule(15)
    sha1_step(e,a,b,c,d, sha1_step_exp1, 0x5A827999u, buffer[ 0]) sha1_schedule( 0)
    sha1_step(d,e,a,b,c, sha1_step_exp1, 0x5A827999u, buffer[ 1]) sha1_schedule( 1)
    sha1_step(c,d,e,a,b, sha1_step_exp1, 0x5A827999u, buffer[ 2]) sha1_schedule( 2)
    sha1_step(b,c,d,e,a, sha1_step_exp1, 0x5A827999u, buffer[ 3]) sha1_schedule( 3)
    sha1_step(a,b,c,d,e, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 4]) sha1_schedule( 4)
    sha1_step(e,a,b,c,d, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 5]) sha1_schedule( 5)
    sha1_step(d,e,a,b,c, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 6]) sha1_schedule( 6)
    sha1_step(c,d,e,a,b, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 7]) sha1_schedule( 7)
    sha1_step(b,c,d,e,a, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 8]) sha1_schedule( 8)
    sha1_step(a,b,c,d,e, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 9]) sha1_schedule( 9)
    sha1_step(e,a,b,c,d, sha1_step_exp2, 0x6ED9EBA1u, buffer[10]) sha1_schedule(10)
    sha1_step(d,e,a,b,c, sha1_step_exp2, 0x6ED9EBA1u, buffer[11]) sha1_schedule(11)
    sha1_step(c,d,e,a,b, sha1_step_exp2, 0x6ED9EBA1u, buffer[12]) sha1_schedule(12)
    sha1_step(b,c,d,e,a, sha1_step_exp2, 0x6ED9EBA1u, buffer[13]) sha1_schedule(13)
    sha1_step(a,b,c,d,e, sha1_step_exp2, 0x6ED9EBA1u, buffer[14]) sha1_schedule(14)
    sha1_step(e,a,b,c,d, sha1_step_exp2, 0x6ED9EBA1u, buffer[15]) sha1_schedule(15)
    sha1_step(d,e,a,b,c, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 0]) sha1_schedule( 0)
    sha1_step(c,d,e,a,b, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 1]) sha1_schedule( 1)
    sha1_step(b,c,d,e,a, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 2]) sha1_schedule( 2)
    sha1_step(a,b,c,d,e, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 3]) sha1_schedule( 3)
    sha1_step(e,a,b,c,d, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 4]) sha1_schedule( 4)
    sha1_step(d,e,a,b,c, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 5]) sha1_schedule( 5)
    sha1_step(c,d,e,a,b, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 6]) sha1_schedule( 6)
    sha1_step(b,c,d,e,a, sha1_step_exp2, 0x6ED9EBA1u, buffer[ 7]) sha1_schedule( 7)
    sha1_step(a,b,c,d,e, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 8]) sha1_schedule( 8)
    sha1_step(e,a,b,c,d, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 9]) sha1_schedule( 9)
    sha1_step(d,e,a,b,c, sha1_step_exp3, 0x8F1BBCDCu, buffer[10]) sha1_schedule(10)
    sha1_step(c,d,e,a,b, sha1_step_exp3, 0x8F1BBCDCu, buffer[11]) sha1_schedule(11)
    sha1_step(b,c,d,e,a, sha1_step_exp3, 0x8F1BBCDCu, buffer[12]) sha1_schedule(12)
    sha1_step(a,b,c,d,e, sha1_step_exp3, 0x8F1BBCDCu, buffer[13]) sha1_schedule(13)
    sha1_step(e,a,b,c,d, sha1_step_exp3, 0x8F1BBCDCu, buffer[14]) sha1_schedule(14)
    sha1_step(d,e,a,b,c, sha1_step_exp3, 0x8F1BBCDCu, buffer[15]) sha1_schedule(15)
    sha1_step(c,d,e,a,b, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 0]) sha1_schedule( 0)
    sha1_step(b,c,d,e,a, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 1]) sha1_schedule( 1)
    sha1_step(a,b,c,d,e, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 2]) sha1_schedule( 2)
    sha1_step(e,a,b,c,d, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 3]) sha1_schedule( 3)
    sha1_step(d,e,a,b,c, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 4]) sha1_schedule( 4)
    sha1_step(c,d,e,a,b, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 5]) sha1_schedule( 5)
    sha1_step(b,c,d,e,a, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 6]) sha1_schedule( 6)
    sha1_step(a,b,c,d,e, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 7]) sha1_schedule( 7)
    sha1_step(e,a,b,c,d, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 8]) sha1_schedule( 8)
    sha1_step(d,e,a,b,c, sha1_step_exp3, 0x8F1BBCDCu, buffer[ 9]) sha1_schedule( 9)
    sha1_step(c,d,e,a,b, sha1_step_exp3, 0x8F1BBCDCu, buffer[10]) sha1_schedule(10)
    sha1_step(b,c,d,e,a, sha1_step_exp3, 0x8F1BBCDCu, buffer[11]) sha1_schedule(11)
    sha1_step(a,b,c,d,e, sha1_step_exp2, 0xCA62C1D6u, buffer[12]) sha1_schedule(12)
    sha1_step(e,a,b,c,d, sha1_step_exp2, 0xCA62C1D6u, buffer[13]) sha1_schedule(13)
    sha1_step(d,e,a,b,c, sha1_step_exp2, 0xCA62C1D6u, buffer[14]) sha1_schedule(14)
    sha1_step(c,d,e,a,b, sha1_step_exp2, 0xCA62C1D6u, buffer[15]) sha1_schedule(15)
    sha1_step(b,c,d,e,a, sha1_step_exp2, 0xCA62C1D6u, buffer[ 0])
    sha1_step(a,b,c,d,e, sha1_step_exp2, 0xCA62C1D6u, buffer[ 1])
    sha1_step(e,a,b,c,d, sha1_step_exp2, 0xCA62C1D6u, buffer[ 2])
    sha1_step(d,e,a,b,c, sha1_step_exp2, 0xCA62C1D6u, buffer[ 3])
    sha1_step(c,d,e,a,b, sha1_step_exp2, 0xCA62C1D6u, buffer[ 4])
    sha1_step(b,c,d,e,a, sha1_step_exp2, 0xCA62C1D6u, buffer[ 5])
    sha1_step(a,b,c,d,e, sha1_step_exp2, 0xCA62C1D6u, buffer[ 6])
    sha1_step(e,a,b,c,d, sha1_step_exp2, 0xCA62C1D6u, buffer[ 7])
    sha1_step(d,e,a,b,c, sha1_step_exp2, 0xCA62C1D6u, buffer[ 8])
    sha1_step(c,d,e,a,b, sha1_step_exp2, 0xCA62C1D6u, buffer[ 9])
    sha1_step(b,c,d,e,a, sha1_step_exp2, 0xCA62C1D6u, buffer[10])
    sha1_step(a,b,c,d,e, sha1_step_exp2, 0xCA62C1D6u, buffer[11])
    sha1_step(e,a,b,c,d, sha1_step_exp2, 0xCA62C1D6u, buffer[12])
    sha1_step(d,e,a,b,c, sha1_step_exp2, 0xCA62C1D6u, buffer[13])
    sha1_step(c,d,e,a,b, sha1_step_exp2, 0xCA62C1D6u, buffer[14])
    sha1_step(b,c,d,e,a, sha1_step_exp2, 0xCA62C1D6u, buffer[15])

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;

    #undef ROTL32
    #undef sha1_schedule
    #undef sha1_step
    #undef sha1_step_exp1
    #undef sha1_step_exp2
    #undef sha1_step_exp3
  }

  void pad() {
    ::std::uint64_t const len = count;
    update_single(0x80);
    while (count % 512 != 448)
      update_single(0x00);
    update_single(static_cast<::std::uint8_t>(len >> 56));
    update_single(static_cast<::std::uint8_t>(len >> 48));
    update_single(static_cast<::std::uint8_t>(len >> 40));
    update_single(static_cast<::std::uint8_t>(len >> 32));
    update_single(static_cast<::std::uint8_t>(len >> 24));
    update_single(static_cast<::std::uint8_t>(len >> 16));
    update_single(static_cast<::std::uint8_t>(len >> 8));
    update_single(static_cast<::std::uint8_t>(len >> 0));
  }

public:
  void update_single(::std::uint8_t const byte) {
    ::std::uint32_t const address = static_cast<::std::uint32_t>(count) % 512 / 32;
    buffer[address] = (buffer[address] << 8) | byte;
    count += 8;
    if (0 == (static_cast<::std::uint32_t>(count) % 512))
      f();
  }

  template <typename T, typename U>
  void update_range(T begin, U const &end) {
    for (; begin != end; ++begin)
      update_single(static_cast<::std::uint8_t>(*begin));
  }

  template <typename T, typename U>
  void store_result(T const &begin, U const &end) {
    pad();

    T iter(begin);
    for (size_t i = 0; i < hashsize / 32; ++i) {
      if(iter == end) { return; } *iter = static_cast< ::std::uint8_t >(state[i] >> 24); ++iter;
      if(iter == end) { return; } *iter = static_cast< ::std::uint8_t >(state[i] >> 16); ++iter;
      if(iter == end) { return; } *iter = static_cast< ::std::uint8_t >(state[i] >>  8); ++iter;
      if(iter == end) { return; } *iter = static_cast< ::std::uint8_t >(state[i] >>  0); ++iter;
    }
  }

  void reset() {
    state[0] = 0x67452301;
    state[1] = 0xEFCDAB89;
    state[2] = 0x98BADCFE;
    state[3] = 0x10325476;
    state[4] = 0xC3D2E1F0;
    count = 0;
  }

  SHA1() = default;
};
}
}
