/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BaseProfiler.h"

#ifdef MOZ_BASE_PROFILER

#  include "mozilla/BlocksRingBuffer.h"
#  include "mozilla/leb128iterator.h"
#  include "mozilla/ModuloBuffer.h"
#  include "mozilla/PowerOfTwo.h"

#  include "mozilla/Attributes.h"
#  include "mozilla/Vector.h"

#  if defined(_MSC_VER)
#    include <windows.h>
#    include <mmsystem.h>
#    include <process.h>
#  else
#    include <time.h>
#    include <unistd.h>
#  endif

#  include <algorithm>
#  include <atomic>
#  include <thread>
#  include <type_traits>

using namespace mozilla;

MOZ_MAYBE_UNUSED static void SleepMilli(unsigned aMilliseconds) {
#  if defined(_MSC_VER)
  Sleep(aMilliseconds);
#  else
  struct timespec ts;
  ts.tv_sec = aMilliseconds / 1000;
  ts.tv_nsec = long(aMilliseconds % 1000) * 1000000;
  struct timespec tr;
  while (nanosleep(&ts, &tr)) {
    if (errno == EINTR) {
      ts = tr;
    } else {
      printf("nanosleep() -> %s\n", strerror(errno));
      exit(1);
    }
  }
#  endif
}

void TestPowerOfTwoMask() {
  printf("TestPowerOfTwoMask...\n");

  static_assert(MakePowerOfTwoMask<uint32_t, 0>().MaskValue() == 0, "");
  constexpr PowerOfTwoMask<uint32_t> c0 = MakePowerOfTwoMask<uint32_t, 0>();
  MOZ_RELEASE_ASSERT(c0.MaskValue() == 0);

  static_assert(MakePowerOfTwoMask<uint32_t, 0xFFu>().MaskValue() == 0xFFu, "");
  constexpr PowerOfTwoMask<uint32_t> cFF =
      MakePowerOfTwoMask<uint32_t, 0xFFu>();
  MOZ_RELEASE_ASSERT(cFF.MaskValue() == 0xFFu);

  static_assert(
      MakePowerOfTwoMask<uint32_t, 0xFFFFFFFFu>().MaskValue() == 0xFFFFFFFFu,
      "");
  constexpr PowerOfTwoMask<uint32_t> cFFFFFFFF =
      MakePowerOfTwoMask<uint32_t, 0xFFFFFFFFu>();
  MOZ_RELEASE_ASSERT(cFFFFFFFF.MaskValue() == 0xFFFFFFFFu);

  struct TestDataU32 {
    uint32_t mInput;
    uint32_t mMask;
  };
  // clang-format off
  TestDataU32 tests[] = {
    { 0, 0 },
    { 1, 1 },
    { 2, 3 },
    { 3, 3 },
    { 4, 7 },
    { 5, 7 },
    { (1u << 31) - 1, (1u << 31) - 1 },
    { (1u << 31), uint32_t(-1) },
    { (1u << 31) + 1, uint32_t(-1) },
    { uint32_t(-1), uint32_t(-1) }
  };
  // clang-format on
  for (const TestDataU32& test : tests) {
    PowerOfTwoMask<uint32_t> p2m(test.mInput);
    MOZ_RELEASE_ASSERT(p2m.MaskValue() == test.mMask);
    for (const TestDataU32& inner : tests) {
      if (p2m.MaskValue() != uint32_t(-1)) {
        MOZ_RELEASE_ASSERT((inner.mInput % p2m) ==
                           (inner.mInput % (p2m.MaskValue() + 1)));
      }
      MOZ_RELEASE_ASSERT((inner.mInput & p2m) == (inner.mInput % p2m));
      MOZ_RELEASE_ASSERT((p2m & inner.mInput) == (inner.mInput & p2m));
    }
  }

  printf("TestPowerOfTwoMask done\n");
}

void TestPowerOfTwo() {
  printf("TestPowerOfTwo...\n");

  static_assert(MakePowerOfTwo<uint32_t, 1>().Value() == 1, "");
  constexpr PowerOfTwo<uint32_t> c1 = MakePowerOfTwo<uint32_t, 1>();
  MOZ_RELEASE_ASSERT(c1.Value() == 1);
  static_assert(MakePowerOfTwo<uint32_t, 1>().Mask().MaskValue() == 0, "");

  static_assert(MakePowerOfTwo<uint32_t, 128>().Value() == 128, "");
  constexpr PowerOfTwo<uint32_t> c128 = MakePowerOfTwo<uint32_t, 128>();
  MOZ_RELEASE_ASSERT(c128.Value() == 128);
  static_assert(MakePowerOfTwo<uint32_t, 128>().Mask().MaskValue() == 127, "");

  static_assert(MakePowerOfTwo<uint32_t, 0x80000000u>().Value() == 0x80000000u,
                "");
  constexpr PowerOfTwo<uint32_t> cMax = MakePowerOfTwo<uint32_t, 0x80000000u>();
  MOZ_RELEASE_ASSERT(cMax.Value() == 0x80000000u);
  static_assert(
      MakePowerOfTwo<uint32_t, 0x80000000u>().Mask().MaskValue() == 0x7FFFFFFFu,
      "");

  struct TestDataU32 {
    uint32_t mInput;
    uint32_t mValue;
    uint32_t mMask;
  };
  // clang-format off
  TestDataU32 tests[] = {
    { 0, 1, 0 },
    { 1, 1, 0 },
    { 2, 2, 1 },
    { 3, 4, 3 },
    { 4, 4, 3 },
    { 5, 8, 7 },
    { (1u << 31) - 1, (1u << 31), (1u << 31) - 1 },
    { (1u << 31), (1u << 31), (1u << 31) - 1 },
    { (1u << 31) + 1, (1u << 31), (1u << 31) - 1 },
    { uint32_t(-1), (1u << 31), (1u << 31) - 1 }
  };
  // clang-format on
  for (const TestDataU32& test : tests) {
    PowerOfTwo<uint32_t> p2(test.mInput);
    MOZ_RELEASE_ASSERT(p2.Value() == test.mValue);
    MOZ_RELEASE_ASSERT(p2.MaskValue() == test.mMask);
    PowerOfTwoMask<uint32_t> p2m = p2.Mask();
    MOZ_RELEASE_ASSERT(p2m.MaskValue() == test.mMask);
    for (const TestDataU32& inner : tests) {
      MOZ_RELEASE_ASSERT((inner.mInput % p2) == (inner.mInput % p2.Value()));
    }
  }

  printf("TestPowerOfTwo done\n");
}

void TestLEB128() {
  printf("TestLEB128...\n");

  MOZ_RELEASE_ASSERT(ULEB128MaxSize<uint8_t>() == 2);
  MOZ_RELEASE_ASSERT(ULEB128MaxSize<uint16_t>() == 3);
  MOZ_RELEASE_ASSERT(ULEB128MaxSize<uint32_t>() == 5);
  MOZ_RELEASE_ASSERT(ULEB128MaxSize<uint64_t>() == 10);

  struct TestDataU64 {
    uint64_t mValue;
    unsigned mSize;
    const char* mBytes;
  };
  // clang-format off
  TestDataU64 tests[] = {
    // Small numbers should keep their normal byte representation.
    {                  0u,  1, "\0" },
    {                  1u,  1, "\x01" },

    // 0111 1111 (127, or 0x7F) is the highest number that fits into a single
    // LEB128 byte. It gets encoded as 0111 1111, note the most significant bit
    // is off.
    {               0x7Fu,  1, "\x7F" },

    // Next number: 128, or 0x80.
    //   Original data representation:  1000 0000
    //     Broken up into groups of 7:         1  0000000
    // Padded with 0 (msB) or 1 (lsB):  00000001 10000000
    //            Byte representation:  0x01     0x80
    //            Little endian order:  -> 0x80 0x01
    {               0x80u,  2, "\x80\x01" },

    // Next: 129, or 0x81 (showing that we don't lose low bits.)
    //   Original data representation:  1000 0001
    //     Broken up into groups of 7:         1  0000001
    // Padded with 0 (msB) or 1 (lsB):  00000001 10000001
    //            Byte representation:  0x01     0x81
    //            Little endian order:  -> 0x81 0x01
    {               0x81u,  2, "\x81\x01" },

    // Highest 8-bit number: 255, or 0xFF.
    //   Original data representation:  1111 1111
    //     Broken up into groups of 7:         1  1111111
    // Padded with 0 (msB) or 1 (lsB):  00000001 11111111
    //            Byte representation:  0x01     0xFF
    //            Little endian order:  -> 0xFF 0x01
    {               0xFFu,  2, "\xFF\x01" },

    // Next: 256, or 0x100.
    //   Original data representation:  1 0000 0000
    //     Broken up into groups of 7:        10  0000000
    // Padded with 0 (msB) or 1 (lsB):  00000010 10000000
    //            Byte representation:  0x10     0x80
    //            Little endian order:  -> 0x80 0x02
    {              0x100u,  2, "\x80\x02" },

    // Highest 32-bit number: 0xFFFFFFFF (8 bytes, all bits set).
    // Original: 1111 1111 1111 1111 1111 1111 1111 1111
    // Groups:     1111  1111111  1111111  1111111  1111111
    // Padded: 00001111 11111111 11111111 11111111 11111111
    // Bytes:  0x0F     0xFF     0xFF     0xFF     0xFF
    // Little Endian: -> 0xFF 0xFF 0xFF 0xFF 0x0F
    {         0xFFFFFFFFu,  5, "\xFF\xFF\xFF\xFF\x0F" },

    // Highest 64-bit number: 0xFFFFFFFFFFFFFFFF (16 bytes, all bits set).
    // 64 bits, that's 9 groups of 7 bits, plus 1 (most significant) bit.
    { 0xFFFFFFFFFFFFFFFFu, 10, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01" }
  };
  // clang-format on

  for (const TestDataU64& test : tests) {
    MOZ_RELEASE_ASSERT(ULEB128Size(test.mValue) == test.mSize);
    // Prepare a buffer that can accomodate the largest-possible LEB128.
    uint8_t buffer[ULEB128MaxSize<uint64_t>()];
    // Use a pointer into the buffer as iterator.
    uint8_t* p = buffer;
    // And write the LEB128.
    WriteULEB128(test.mValue, p);
    // Pointer (iterator) should have advanced just past the expected LEB128
    // size.
    MOZ_RELEASE_ASSERT(p == buffer + test.mSize);
    // Check expected bytes.
    for (unsigned i = 0; i < test.mSize; ++i) {
      MOZ_RELEASE_ASSERT(buffer[i] == uint8_t(test.mBytes[i]));
    }
    // Move pointer (iterator) back to start of buffer.
    p = buffer;
    // And read the LEB128 we wrote above.
    uint64_t read = ReadULEB128<uint64_t>(p);
    // Pointer (iterator) should have also advanced just past the expected
    // LEB128 size.
    MOZ_RELEASE_ASSERT(p == buffer + test.mSize);
    // And check the read value.
    MOZ_RELEASE_ASSERT(read == test.mValue);
  }

  printf("TestLEB128 done\n");
}

void TestModuloBuffer() {
  printf("TestModuloBuffer...\n");

  // Testing ModuloBuffer with default template arguments.
  using MB = ModuloBuffer<>;

  // Only 8-byte buffer, to easily test wrap-around.
  constexpr uint32_t MBSize = 8;
  MB mb(MakePowerOfTwo32<MBSize>());

  MOZ_RELEASE_ASSERT(mb.BufferLength().Value() == MBSize);

  // Iterator comparisons.
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) == mb.ReaderAt(2));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) != mb.ReaderAt(3));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) < mb.ReaderAt(3));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) <= mb.ReaderAt(2));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) <= mb.ReaderAt(3));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(3) > mb.ReaderAt(2));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) >= mb.ReaderAt(2));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(3) >= mb.ReaderAt(2));

  // Iterators indices don't wrap around (even though they may be pointing at
  // the same location).
  MOZ_RELEASE_ASSERT(mb.ReaderAt(2) != mb.ReaderAt(MBSize + 2));
  MOZ_RELEASE_ASSERT(mb.ReaderAt(MBSize + 2) != mb.ReaderAt(2));

  // Dereference.
  static_assert(std::is_same<decltype(*mb.ReaderAt(0)), const MB::Byte&>::value,
                "Dereferencing from a reader should return const Byte*");
  static_assert(std::is_same<decltype(*mb.WriterAt(0)), MB::Byte&>::value,
                "Dereferencing from a writer should return Byte*");
  // Contiguous between 0 and MBSize-1.
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(MBSize - 1) ==
                     &*mb.ReaderAt(0) + (MBSize - 1));
  // Wraps around.
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(MBSize) == &*mb.ReaderAt(0));
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(MBSize + MBSize - 1) ==
                     &*mb.ReaderAt(MBSize - 1));
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(MBSize + MBSize) == &*mb.ReaderAt(0));
  // Power of 2 modulo wrapping.
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(uint32_t(-1)) == &*mb.ReaderAt(MBSize - 1));
  MOZ_RELEASE_ASSERT(&*mb.ReaderAt(static_cast<MB::Index>(-1)) ==
                     &*mb.ReaderAt(MBSize - 1));

  // Arithmetic.
  MB::Reader arit = mb.ReaderAt(0);
  MOZ_RELEASE_ASSERT(++arit == mb.ReaderAt(1));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(1));

  MOZ_RELEASE_ASSERT(--arit == mb.ReaderAt(0));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(0));

  MOZ_RELEASE_ASSERT(arit + 3 == mb.ReaderAt(3));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(0));

  // (Can't have assignments inside asserts, hence the split.)
  const bool checkPlusEq = ((arit += 3) == mb.ReaderAt(3));
  MOZ_RELEASE_ASSERT(checkPlusEq);
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(3));

  MOZ_RELEASE_ASSERT((arit - 2) == mb.ReaderAt(1));
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(3));

  const bool checkMinusEq = ((arit -= 2) == mb.ReaderAt(1));
  MOZ_RELEASE_ASSERT(checkMinusEq);
  MOZ_RELEASE_ASSERT(arit == mb.ReaderAt(1));

  // Iterator difference.
  MOZ_RELEASE_ASSERT(mb.ReaderAt(3) - mb.ReaderAt(1) == 2);
  MOZ_RELEASE_ASSERT(mb.ReaderAt(1) - mb.ReaderAt(3) == MB::Index(-2));

  // Only testing Writer, as Reader is just a subset with no code differences.
  MB::Writer it = mb.WriterAt(0);
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 0);

  // Write two characters at the start.
  it.WriteObject('x');
  it.WriteObject('y');

  // Backtrack to read them.
  it -= 2;
  // PeekObject should read without moving.
  MOZ_RELEASE_ASSERT(it.PeekObject<char>() == 'x');
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 0);
  // ReadObject should read and move past the character.
  MOZ_RELEASE_ASSERT(it.ReadObject<char>() == 'x');
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 1);
  MOZ_RELEASE_ASSERT(it.PeekObject<char>() == 'y');
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 1);
  MOZ_RELEASE_ASSERT(it.ReadObject<char>() == 'y');
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 2);

  // Checking that a reader can be created from a writer.
  MB::Reader it2(it);
  MOZ_RELEASE_ASSERT(it2.CurrentIndex() == 2);
  // Or assigned.
  it2 = it;
  MOZ_RELEASE_ASSERT(it2.CurrentIndex() == 2);

  // Write 4-byte number at index 2.
  it.WriteObject(int32_t(123));
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == 6);
  // And another, which should now wrap around (but index continues on.)
  it.WriteObject(int32_t(456));
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == MBSize + 2);
  // Even though index==MBSize+2, we can read the object we wrote at 2.
  MOZ_RELEASE_ASSERT(it.ReadObject<int32_t>() == 123);
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == MBSize + 6);
  // And similarly, index MBSize+6 points at the same location as index 6.
  MOZ_RELEASE_ASSERT(it.ReadObject<int32_t>() == 456);
  MOZ_RELEASE_ASSERT(it.CurrentIndex() == MBSize + MBSize + 2);

  printf("TestModuloBuffer done\n");
}

// Backdoor into value of BlockIndex, only for unit-testing.
static uint64_t ExtractBlockIndex(const BlocksRingBuffer::BlockIndex bi) {
  uint64_t index;
  static_assert(sizeof(bi) == sizeof(index),
                "BlockIndex expected to only contain a uint64_t");
  memcpy(&index, &bi, sizeof(index));
  return index;
};

void TestBlocksRingBufferAPI() {
  printf("TestBlocksRingBufferAPI...\n");

  // Deleter will store about-to-be-deleted value in `lastDestroyed`.
  uint32_t lastDestroyed = 0;

  // Start a temporary block to constrain buffer lifetime.
  {
    // Create a 16-byte buffer, enough to store up to 3 entries (1 byte size + 4
    // bytes uint64_t).
    BlocksRingBuffer rb(MakePowerOfTwo32<16>(),
                        [&](BlocksRingBuffer::EntryReader aReader) {
                          lastDestroyed = aReader.ReadObject<uint32_t>();
                        });

#  define VERIFY_START_END_DESTROYED(aStart, aEnd, aLastDestroyed)        \
    rb.Read([&](const BlocksRingBuffer::Reader aReader) {                 \
      MOZ_RELEASE_ASSERT(ExtractBlockIndex(aReader.BufferRangeStart()) == \
                         (aStart));                                       \
      MOZ_RELEASE_ASSERT(ExtractBlockIndex(aReader.BufferRangeEnd()) ==   \
                         (aEnd));                                         \
      MOZ_RELEASE_ASSERT(lastDestroyed == (aLastDestroyed));              \
    });

    // Empty buffer to start with.
    // Start&end indices still at 0, nothing destroyed.
    VERIFY_START_END_DESTROYED(0, 0, 0);

    // All entries will contain one 32-bit number. The resulting blocks will
    // have the following structure:
    // - 1 byte for the LEB128 size of 4
    // - 4 bytes for the number.
    // E.g., if we have entries with `123` and `456`:
    // .-- first readable block at index 0
    // |.-- first block at index 0
    // ||.-- 1 byte for the entry size, which is `4` (32 bits)
    // |||  .-- entry starts at index 1, contain 32-bit int
    // |||  |             .-- entry and block finish *after* index 4, i.e., 5
    // |||  |             | .-- second block starts at index 5
    // |||  |             | |         etc.
    // |||  |             | |                  .-- End of readable blocks at 10
    // vvv  v             v V                  v
    //   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
    // S[4 |   int(123)   ] [4 |   int(456)   ]E

    // Push `1` directly.
    MOZ_RELEASE_ASSERT(ExtractBlockIndex(rb.PutObject(uint32_t(1))) == 0);
    //   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
    // S[4 |    int(1)    ]E
    VERIFY_START_END_DESTROYED(0, 5, 0);

    // Push `2` through EntryReserver, check output BlockIndex.
    auto bi2 = rb.Put([](BlocksRingBuffer::EntryReserver aER) {
      return aER.WriteObject(uint32_t(2));
    });
    static_assert(
        std::is_same<decltype(bi2), BlocksRingBuffer::BlockIndex>::value,
        "All index-returning functions should return a "
        "BlocksRingBuffer::BlockIndex");
    MOZ_RELEASE_ASSERT(ExtractBlockIndex(bi2) == 5);
    //   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
    // S[4 |    int(1)    ] [4 |    int(2)    ]E
    VERIFY_START_END_DESTROYED(0, 10, 0);

    // Check single entry at bi2, store next block index.
    auto bi2Next =
        rb.ReadAt(bi2, [](Maybe<BlocksRingBuffer::EntryReader>&& aMaybeReader) {
          MOZ_RELEASE_ASSERT(aMaybeReader.isSome());
          MOZ_RELEASE_ASSERT(aMaybeReader->ReadObject<uint32_t>() == 2);
          MOZ_RELEASE_ASSERT(
              aMaybeReader->GetEntryAt(aMaybeReader->NextBlockIndex())
                  .isNothing());
          return aMaybeReader->NextBlockIndex();
        });
    // bi2Next is at the end, nothing to read.
    rb.ReadAt(bi2Next, [](Maybe<BlocksRingBuffer::EntryReader>&& aMaybeReader) {
      MOZ_RELEASE_ASSERT(aMaybeReader.isNothing());
    });

    // Push `3` through EntryReserver and then EntryWriter, check writer output
    // is returned to the initial caller.
    auto put3 = rb.Put([&](BlocksRingBuffer::EntryReserver aER) {
      return aER.Reserve(
          sizeof(uint32_t), [&](BlocksRingBuffer::EntryWriter aEW) {
            aEW.WriteObject(uint32_t(3));
            return float(ExtractBlockIndex(aEW.CurrentBlockIndex()));
          });
    });
    static_assert(std::is_same<decltype(put3), float>::value,
                  "Expect float as returned by callback.");
    MOZ_RELEASE_ASSERT(put3 == 10.0);
    //   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
    // S[4 |    int(1)    ] [4 |    int(2)    ] [4 |    int(3)    ]E
    VERIFY_START_END_DESTROYED(0, 15, 0);

    // Re-Read single entry at bi2, should now have a next entry.
    rb.ReadAt(bi2, [&](Maybe<BlocksRingBuffer::EntryReader>&& aMaybeReader) {
      MOZ_RELEASE_ASSERT(aMaybeReader.isSome());
      MOZ_RELEASE_ASSERT(aMaybeReader->ReadObject<uint32_t>() == 2);
      MOZ_RELEASE_ASSERT(aMaybeReader->NextBlockIndex() == bi2Next);
      MOZ_RELEASE_ASSERT(aMaybeReader->GetNextEntry().isSome());
      MOZ_RELEASE_ASSERT(
          aMaybeReader->GetEntryAt(aMaybeReader->NextBlockIndex()).isSome());
      MOZ_RELEASE_ASSERT(
          aMaybeReader->GetNextEntry()->CurrentBlockIndex() ==
          aMaybeReader->GetEntryAt(aMaybeReader->NextBlockIndex())
              ->CurrentBlockIndex());
      MOZ_RELEASE_ASSERT(
          aMaybeReader->GetEntryAt(aMaybeReader->NextBlockIndex())
              ->ReadObject<uint32_t>() == 3);
    });

    // Check that we have `1` to `3`.
    uint32_t count = 0;
    rb.ReadEach([&](BlocksRingBuffer::EntryReader aReader) {
      MOZ_RELEASE_ASSERT(aReader.ReadObject<uint32_t>() == ++count);
    });
    MOZ_RELEASE_ASSERT(count == 3);

    // Push `4`, store its BlockIndex for later.
    // This will wrap around, and destroy the first entry.
    BlocksRingBuffer::BlockIndex bi4 = rb.PutObject(uint32_t(4));
    // Before:
    //   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
    // S[4 |    int(1)    ] [4 |    int(2)    ] [4 |    int(3)    ]E
    // 1. First entry destroyed:
    //   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
    //   ?   ?   ?   ?   ? S[4 |    int(2)    ] [4 |    int(3)    ]E
    // 2. New entry starts at 15 and wraps around: (shown on separate line)
    //   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
    //   ?   ?   ?   ?   ? S[4 |    int(2)    ] [4 |    int(3)    ] [4 |
    //  16  17  18  19  20  21  ...
    //      int(4)    ]E
    // (collapsed)
    //  16  17  18  19  20   5   6   7   8   9  10  11  12  13  14  15
    //      int(4)    ]E ? S[4 |    int(2)    ] [4 |    int(3)    ] [4 |
    VERIFY_START_END_DESTROYED(5, 20, 1);

    // Check that we have `2` to `4`.
    count = 1;
    rb.ReadEach([&](BlocksRingBuffer::EntryReader aReader) {
      MOZ_RELEASE_ASSERT(aReader.ReadObject<uint32_t>() == ++count);
    });
    MOZ_RELEASE_ASSERT(count == 4);

    // Push 5 through EntryReserver then EntryWriter, no returns.
    // This will destroy the second entry.
    // Check that the EntryWriter can access bi4 but not bi2.
    auto bi5 = rb.Put([&](BlocksRingBuffer::EntryReserver aER) {
      return aER.Reserve(
          sizeof(uint32_t), [&](BlocksRingBuffer::EntryWriter aEW) {
            aEW.WriteObject(uint32_t(5));
            MOZ_RELEASE_ASSERT(aEW.GetEntryAt(bi2).isNothing());
            MOZ_RELEASE_ASSERT(aEW.GetEntryAt(bi4).isSome());
            MOZ_RELEASE_ASSERT(aEW.GetEntryAt(bi4)->CurrentBlockIndex() == bi4);
            MOZ_RELEASE_ASSERT(aEW.GetEntryAt(bi4)->ReadObject<uint32_t>() ==
                               4);
            return aEW.CurrentBlockIndex();
          });
    });
    //  16  17  18  19  20  21  22  23  24  25  10  11  12  13  14  15
    //      int(4)    ] [4 |    int(5)    ]E ? S[4 |    int(3)    ] [4 |
    VERIFY_START_END_DESTROYED(10, 25, 2);

    // Read single entry at bi2, should now gracefully fail.
    rb.ReadAt(bi2, [](Maybe<BlocksRingBuffer::EntryReader>&& aMaybeReader) {
      MOZ_RELEASE_ASSERT(aMaybeReader.isNothing());
    });

    // Read single entry at bi5.
    rb.ReadAt(bi5, [](Maybe<BlocksRingBuffer::EntryReader>&& aMaybeReader) {
      MOZ_RELEASE_ASSERT(aMaybeReader.isSome());
      MOZ_RELEASE_ASSERT(aMaybeReader->ReadObject<uint32_t>() == 5);
      MOZ_RELEASE_ASSERT(
          aMaybeReader->GetEntryAt(aMaybeReader->NextBlockIndex()).isNothing());
    });

    // Check that we have `3` to `5`.
    count = 2;
    rb.ReadEach([&](BlocksRingBuffer::EntryReader aReader) {
      MOZ_RELEASE_ASSERT(aReader.ReadObject<uint32_t>() == ++count);
    });
    MOZ_RELEASE_ASSERT(count == 5);

    // Delete everything before `4`, this should delete `3`.
    rb.ClearBefore(bi4);
    //  16  17  18  19  20  21  22  23  24  25  10  11  12  13  14  15
    //      int(4)    ] [4 |    int(5)    ]E ?   ?   ?   ?   ?   ? S[4 |
    VERIFY_START_END_DESTROYED(15, 25, 3);

    // Check that we have `4` to `5`.
    count = 3;
    rb.ReadEach([&](BlocksRingBuffer::EntryReader aReader) {
      MOZ_RELEASE_ASSERT(aReader.ReadObject<uint32_t>() == ++count);
    });
    MOZ_RELEASE_ASSERT(count == 5);

    // Delete everything before `4` again, nothing to delete.
    lastDestroyed = 0;
    rb.ClearBefore(bi4);
    VERIFY_START_END_DESTROYED(15, 25, 0);

    // Delete everything, this should delete `4` and `5`, and bring the start
    // index where the end index currently is.
    rb.Clear();
    //  16  17  18  19  20  21  22  23  24  25  10  11  12  13  14  15
    //   ?   ?   ?   ?   ?   ?   ?   ?   ?S E?   ?   ?   ?   ?   ?   ?
    VERIFY_START_END_DESTROYED(25, 25, 5);

    // Check that we have nothing to read.
    rb.ReadEach([&](auto&&) { MOZ_RELEASE_ASSERT(false); });

    // Read single entry at bi5, should now gracefully fail.
    rb.ReadAt(bi5, [](Maybe<BlocksRingBuffer::EntryReader>&& aMaybeReader) {
      MOZ_RELEASE_ASSERT(aMaybeReader.isNothing());
    });

    // Delete everything before now-deleted `4`, nothing to delete.
    lastDestroyed = 0;
    rb.ClearBefore(bi4);
    VERIFY_START_END_DESTROYED(25, 25, 0);

    // Push `6` directly.
    MOZ_RELEASE_ASSERT(ExtractBlockIndex(rb.PutObject(uint32_t(6))) == 25);
    //  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31
    //   ?   ?   ?   ?   ?   ?   ?   ?   ? S[4 |    int(6)    ]E ?   ?
    VERIFY_START_END_DESTROYED(25, 30, 0);

    // End of block where rb lives, should call deleter on destruction.
  }
  MOZ_RELEASE_ASSERT(lastDestroyed == 6);

  printf("TestBlocksRingBufferAPI done\n");
}

void TestBlocksRingBufferThreading() {
  printf("TestBlocksRingBufferThreading...\n");

  // Deleter will store about-to-be-deleted value in `lastDestroyed`.
  std::atomic<int> lastDestroyed{0};

  BlocksRingBuffer rb(MakePowerOfTwo32<8192>(),
                      [&](BlocksRingBuffer::EntryReader aReader) {
                        lastDestroyed = aReader.ReadObject<int>();
                      });

  // Start reader thread.
  std::atomic<bool> stopReader{false};
  std::thread reader([&]() {
    for (;;) {
      Pair<uint64_t, uint64_t> counts = rb.GetPushedAndDeletedCounts();
      printf("Reader: pushed=%llu deleted=%llu alive=%llu lastDestroyed=%d\n",
             static_cast<unsigned long long>(counts.first()),
             static_cast<unsigned long long>(counts.second()),
             static_cast<unsigned long long>(counts.first() - counts.second()),
             int(lastDestroyed));
      if (stopReader) {
        break;
      }
      ::SleepMilli(1);
    }
  });

  // Start writer threads.
  constexpr int ThreadCount = 32;
  std::thread threads[ThreadCount];
  for (int threadNo = 0; threadNo < ThreadCount; ++threadNo) {
    threads[threadNo] = std::thread(
        [&](int aThreadNo) {
          ::SleepMilli(1);
          constexpr int pushCount = 1024;
          for (int push = 0; push < pushCount; ++push) {
            // Reserve as many bytes as the thread number (but at least enough
            // to store an int), and write an increasing int.
            rb.Put(std::max(aThreadNo, int(sizeof(push))),
                   [&](BlocksRingBuffer::EntryWriter aEW) {
                     aEW.WriteObject(aThreadNo * 1000000 + push);
                     aEW += aEW.RemainingBytes();
                   });
          }
        },
        threadNo);
  }

  // Wait for all writer threads to die.
  for (auto&& thread : threads) {
    thread.join();
  }

  // Stop reader thread.
  stopReader = true;
  reader.join();

  printf("TestBlocksRingBufferThreading done\n");
}

// Increase the depth, to a maximum (to avoid too-deep recursion).
static constexpr size_t NextDepth(size_t aDepth) {
  constexpr size_t MAX_DEPTH = 128;
  return (aDepth < MAX_DEPTH) ? (aDepth + 1) : aDepth;
}

// Compute fibonacci the hard way (recursively: `f(n)=f(n-1)+f(n-2)`), and
// prevent inlining.
// The template parameter makes each depth be a separate function, to better
// distinguish them in the profiler output.
template <size_t DEPTH = 0>
MOZ_NEVER_INLINE unsigned long long Fibonacci(unsigned long long n) {
  if (n == 0) {
    return 0;
  }
  if (n == 1) {
    return 1;
  }
  unsigned long long f2 = Fibonacci<NextDepth(DEPTH)>(n - 2);
  if (DEPTH == 0) {
    BASE_PROFILER_ADD_MARKER("Half-way through Fibonacci", OTHER);
  }
  unsigned long long f1 = Fibonacci<NextDepth(DEPTH)>(n - 1);
  return f2 + f1;
}

void TestProfiler() {
  printf("TestProfiler starting -- pid: %d, tid: %d\n",
         baseprofiler::profiler_current_process_id(),
         baseprofiler::profiler_current_thread_id());
  // ::SleepMilli(10000);

  // Test dependencies.
  TestPowerOfTwoMask();
  TestPowerOfTwo();
  TestLEB128();
  TestModuloBuffer();
  TestBlocksRingBufferAPI();
  TestBlocksRingBufferThreading();

  {
    printf("profiler_init()...\n");
    AUTO_BASE_PROFILER_INIT;

    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_is_active());
    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_thread_is_being_profiled());
    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_thread_is_sleeping());

    printf("profiler_start()...\n");
    Vector<const char*> filters;
    // Profile all registered threads.
    MOZ_RELEASE_ASSERT(filters.append(""));
    const uint32_t features = baseprofiler::ProfilerFeature::Leaf |
                              baseprofiler::ProfilerFeature::StackWalk |
                              baseprofiler::ProfilerFeature::Threads;
    baseprofiler::profiler_start(baseprofiler::BASE_PROFILER_DEFAULT_ENTRIES,
                                 BASE_PROFILER_DEFAULT_INTERVAL, features,
                                 filters.begin(), filters.length());

    MOZ_RELEASE_ASSERT(baseprofiler::profiler_is_active());
    MOZ_RELEASE_ASSERT(baseprofiler::profiler_thread_is_being_profiled());
    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_thread_is_sleeping());

    {
      AUTO_BASE_PROFILER_TEXT_MARKER_CAUSE("fibonacci", "First leaf call",
                                           OTHER, nullptr);
      static const unsigned long long fibStart = 40;
      printf("Fibonacci(%llu)...\n", fibStart);
      AUTO_BASE_PROFILER_LABEL("Label around Fibonacci", OTHER);
      unsigned long long f = Fibonacci(fibStart);
      printf("Fibonacci(%llu) = %llu\n", fibStart, f);
    }

    printf("Sleep 1s...\n");
    {
      AUTO_BASE_PROFILER_THREAD_SLEEP;
      SleepMilli(1000);
    }

    printf("baseprofiler_save_profile_to_file()...\n");
    baseprofiler::profiler_save_profile_to_file("TestProfiler_profile.json");

    printf("profiler_stop()...\n");
    baseprofiler::profiler_stop();

    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_is_active());
    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_thread_is_being_profiled());
    MOZ_RELEASE_ASSERT(!baseprofiler::profiler_thread_is_sleeping());

    printf("profiler_shutdown()...\n");
  }

  printf("TestProfiler done\n");
}

#else  // MOZ_BASE_PROFILER

// Testing that macros are still #defined (but do nothing) when
// MOZ_BASE_PROFILER is disabled.
void TestProfiler() {
  // These don't need to make sense, we just want to know that they're defined
  // and don't do anything.
  AUTO_BASE_PROFILER_INIT;

  // This wouldn't build if the macro did output its arguments.
  AUTO_BASE_PROFILER_TEXT_MARKER_CAUSE(catch, catch, catch, catch);

  AUTO_BASE_PROFILER_LABEL(catch, catch);

  AUTO_BASE_PROFILER_THREAD_SLEEP;
}

#endif  // MOZ_BASE_PROFILER else

int main() {
  // Note that there are two `TestProfiler` functions above, depending on
  // whether MOZ_BASE_PROFILER is #defined.
  TestProfiler();

  return 0;
}
