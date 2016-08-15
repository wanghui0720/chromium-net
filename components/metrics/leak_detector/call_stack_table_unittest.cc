// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/leak_detector/call_stack_table.h"

#include <memory>
#include <set>

#include "base/macros.h"
#include "components/metrics/leak_detector/call_stack_manager.h"
#include "components/metrics/leak_detector/custom_allocator.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace metrics {
namespace leak_detector {

namespace {

// Default threshold used for leak analysis.
const int kDefaultLeakThreshold = 5;

// Some test call stacks.
const void* kRawStack0[] = {
    reinterpret_cast<const void*>(0xaabbccdd),
    reinterpret_cast<const void*>(0x11223344),
    reinterpret_cast<const void*>(0x55667788),
    reinterpret_cast<const void*>(0x99887766),
};
const void* kRawStack1[] = {
    reinterpret_cast<const void*>(0xdeadbeef),
    reinterpret_cast<const void*>(0x900df00d),
    reinterpret_cast<const void*>(0xcafedeed),
    reinterpret_cast<const void*>(0xdeafbabe),
};
const void* kRawStack2[] = {
    reinterpret_cast<const void*>(0x12345678),
    reinterpret_cast<const void*>(0xabcdef01),
    reinterpret_cast<const void*>(0xfdecab98),
};
const void* kRawStack3[] = {
    reinterpret_cast<const void*>(0xdead0001),
    reinterpret_cast<const void*>(0xbeef0002),
    reinterpret_cast<const void*>(0x900d0003),
    reinterpret_cast<const void*>(0xf00d0004),
    reinterpret_cast<const void*>(0xcafe0005),
    reinterpret_cast<const void*>(0xdeed0006),
    reinterpret_cast<const void*>(0xdeaf0007),
    reinterpret_cast<const void*>(0xbabe0008),
};

}  // namespace

class CallStackTableTest : public ::testing::Test {
 public:
  CallStackTableTest()
      : stack0_(nullptr),
        stack1_(nullptr),
        stack2_(nullptr),
        stack3_(nullptr) {}

  void SetUp() override {
    CustomAllocator::Initialize();

    manager_.reset(new CallStackManager);

    // The unit tests expect a certain order to the call stack pointers. It is
    // an important detail when checking the output of LeakAnalyzer's suspected
    // leaks, which are ordered by the leak value (call stack pointer). Use a
    // set to sort the pointers as they are created.
    std::set<const CallStack*> stacks;
    stacks.insert(manager_->GetCallStack(arraysize(kRawStack0), kRawStack0));
    stacks.insert(manager_->GetCallStack(arraysize(kRawStack1), kRawStack1));
    stacks.insert(manager_->GetCallStack(arraysize(kRawStack2), kRawStack2));
    stacks.insert(manager_->GetCallStack(arraysize(kRawStack3), kRawStack3));
    ASSERT_EQ(4U, stacks.size());

    std::set<const CallStack*>::const_iterator iter = stacks.begin();
    stack0_ = *iter++;
    stack1_ = *iter++;
    stack2_ = *iter++;
    stack3_ = *iter++;
  }

  void TearDown() override {
    // All call stacks generated by |manager_| will be invalidated when it is
    // destroyed.
    stack0_ = nullptr;
    stack1_ = nullptr;
    stack2_ = nullptr;
    stack3_ = nullptr;

    // Destroy the call stack manager before shutting down the allocator.
    manager_.reset();

    EXPECT_TRUE(CustomAllocator::Shutdown());
  }

 protected:
  // Unit tests should directly reference these pointers to CallStack objects.
  const CallStack* stack0_;
  const CallStack* stack1_;
  const CallStack* stack2_;
  const CallStack* stack3_;

 private:
  std::unique_ptr<CallStackManager> manager_;

  DISALLOW_COPY_AND_ASSIGN(CallStackTableTest);
};

TEST_F(CallStackTableTest, PointerOrder) {
  EXPECT_LT(stack0_, stack1_);
  EXPECT_LT(stack1_, stack2_);
  EXPECT_LT(stack2_, stack3_);
}

TEST_F(CallStackTableTest, EmptyTable) {
  CallStackTable table(kDefaultLeakThreshold);
  EXPECT_TRUE(table.empty());

  EXPECT_EQ(0U, table.num_allocs());
  EXPECT_EQ(0U, table.num_frees());

  // The table should be able to gracefully handle an attempt to remove a call
  // stack entry when none exists.
  table.Remove(stack0_);
  table.Remove(stack1_);
  table.Remove(stack2_);
  table.Remove(stack3_);

  EXPECT_EQ(0U, table.num_allocs());
  EXPECT_EQ(0U, table.num_frees());
}

TEST_F(CallStackTableTest, InsertionAndRemoval) {
  CallStackTable table(kDefaultLeakThreshold);

  table.Add(stack0_);
  EXPECT_EQ(1U, table.size());
  EXPECT_EQ(1U, table.num_allocs());
  table.Add(stack1_);
  EXPECT_EQ(2U, table.size());
  EXPECT_EQ(2U, table.num_allocs());
  table.Add(stack2_);
  EXPECT_EQ(3U, table.size());
  EXPECT_EQ(3U, table.num_allocs());
  table.Add(stack3_);
  EXPECT_EQ(4U, table.size());
  EXPECT_EQ(4U, table.num_allocs());

  // Add some call stacks that have already been added. There should be no
  // change in the number of entries, as they are aggregated by call stack.
  table.Add(stack2_);
  EXPECT_EQ(4U, table.size());
  EXPECT_EQ(5U, table.num_allocs());
  table.Add(stack3_);
  EXPECT_EQ(4U, table.size());
  EXPECT_EQ(6U, table.num_allocs());

  // Start removing entries.
  EXPECT_EQ(0U, table.num_frees());

  table.Remove(stack0_);
  EXPECT_EQ(3U, table.size());
  EXPECT_EQ(1U, table.num_frees());
  table.Remove(stack1_);
  EXPECT_EQ(2U, table.size());
  EXPECT_EQ(2U, table.num_frees());

  // Removing call stacks with multiple counts will not reduce the overall
  // number of table entries, until the count reaches 0.
  table.Remove(stack2_);
  EXPECT_EQ(2U, table.size());
  EXPECT_EQ(3U, table.num_frees());
  table.Remove(stack3_);
  EXPECT_EQ(2U, table.size());
  EXPECT_EQ(4U, table.num_frees());

  table.Remove(stack2_);
  EXPECT_EQ(1U, table.size());
  EXPECT_EQ(5U, table.num_frees());
  table.Remove(stack3_);
  EXPECT_EQ(0U, table.size());
  EXPECT_EQ(6U, table.num_frees());

  // Now the table should be empty, but attempt to remove some more and make
  // sure nothing breaks.
  table.Remove(stack0_);
  table.Remove(stack1_);
  table.Remove(stack2_);
  table.Remove(stack3_);

  EXPECT_TRUE(table.empty());
  EXPECT_EQ(6U, table.num_allocs());
  EXPECT_EQ(6U, table.num_frees());
}

TEST_F(CallStackTableTest, MassiveInsertionAndRemoval) {
  CallStackTable table(kDefaultLeakThreshold);

  for (int i = 0; i < 100; ++i)
    table.Add(stack3_);
  EXPECT_EQ(1U, table.size());
  EXPECT_EQ(100U, table.num_allocs());

  for (int i = 0; i < 100; ++i)
    table.Add(stack2_);
  EXPECT_EQ(2U, table.size());
  EXPECT_EQ(200U, table.num_allocs());

  for (int i = 0; i < 100; ++i)
    table.Add(stack1_);
  EXPECT_EQ(3U, table.size());
  EXPECT_EQ(300U, table.num_allocs());

  for (int i = 0; i < 100; ++i)
    table.Add(stack0_);
  EXPECT_EQ(4U, table.size());
  EXPECT_EQ(400U, table.num_allocs());

  // Remove them in a different order, by removing one of each stack during one
  // iteration. The size should not decrease until the last iteration.
  EXPECT_EQ(0U, table.num_frees());

  for (int i = 0; i < 100; ++i) {
    table.Remove(stack0_);
    EXPECT_EQ(4U * i + 1, table.num_frees());

    table.Remove(stack1_);
    EXPECT_EQ(4U * i + 2, table.num_frees());

    table.Remove(stack2_);
    EXPECT_EQ(4U * i + 3, table.num_frees());

    table.Remove(stack3_);
    EXPECT_EQ(4U * i + 4, table.num_frees());
  }
  EXPECT_EQ(400U, table.num_frees());
  EXPECT_TRUE(table.empty());

  // Try to remove some more from an empty table and make sure nothing breaks.
  table.Remove(stack0_);
  table.Remove(stack1_);
  table.Remove(stack2_);
  table.Remove(stack3_);

  EXPECT_TRUE(table.empty());
  EXPECT_EQ(400U, table.num_allocs());
  EXPECT_EQ(400U, table.num_frees());
}

TEST_F(CallStackTableTest, DetectLeak) {
  CallStackTable table(kDefaultLeakThreshold);

  // Add some base number of entries.
  for (int i = 0; i < 60; ++i)
    table.Add(stack0_);
  for (int i = 0; i < 50; ++i)
    table.Add(stack1_);
  for (int i = 0; i < 64; ++i)
    table.Add(stack2_);
  for (int i = 0; i < 72; ++i)
    table.Add(stack3_);

  table.TestForLeaks();
  EXPECT_TRUE(table.leak_analyzer().suspected_leaks().empty());

  // Use the following scheme:
  // - stack0_: increase by 4 each time -- leak suspect
  // - stack1_: increase by 3 each time -- leak suspect
  // - stack2_: increase by 1 each time -- not a suspect
  // - stack3_: alternate between increasing and decreasing - not a suspect
  bool increase_kstack3 = true;
  for (int i = 0; i < kDefaultLeakThreshold; ++i) {
    EXPECT_TRUE(table.leak_analyzer().suspected_leaks().empty());

    for (int j = 0; j < 4; ++j)
      table.Add(stack0_);

    for (int j = 0; j < 3; ++j)
      table.Add(stack1_);

    table.Add(stack2_);

    // Alternate between adding and removing.
    if (increase_kstack3)
      table.Add(stack3_);
    else
      table.Remove(stack3_);
    increase_kstack3 = !increase_kstack3;

    table.TestForLeaks();
  }

  // Check that the correct leak values have been detected.
  const auto& leaks = table.leak_analyzer().suspected_leaks();
  ASSERT_EQ(2U, leaks.size());
  // Suspected leaks are reported in increasing leak value -- in this case, the
  // CallStack object's address.
  EXPECT_EQ(stack0_, leaks[0].call_stack());
  EXPECT_EQ(stack1_, leaks[1].call_stack());
}

TEST_F(CallStackTableTest, GetTopCallStacks) {
  CallStackTable table(kDefaultLeakThreshold);

  // Add a bunch of entries.
  for (int i = 0; i < 60; ++i)
    table.Add(stack0_);
  for (int i = 0; i < 50; ++i)
    table.Add(stack1_);
  for (int i = 0; i < 64; ++i)
    table.Add(stack2_);
  for (int i = 0; i < 72; ++i)
    table.Add(stack3_);

  // Get the call sites ordered from least to greatest number of entries.
  RankedSet top_four(4);
  table.GetTopCallStacks(&top_four);
  ASSERT_EQ(4U, top_four.size());
  auto iter = top_four.begin();
  EXPECT_EQ(72, iter->count);
  EXPECT_EQ(stack3_, iter->value.call_stack());
  ++iter;
  EXPECT_EQ(64, iter->count);
  EXPECT_EQ(stack2_, iter->value.call_stack());
  ++iter;
  EXPECT_EQ(60, iter->count);
  EXPECT_EQ(stack0_, iter->value.call_stack());
  ++iter;
  EXPECT_EQ(50, iter->count);
  EXPECT_EQ(stack1_, iter->value.call_stack());

  // Get the top three call sites ordered from least to greatest number of
  // entries.
  RankedSet top_three(3);
  table.GetTopCallStacks(&top_three);
  ASSERT_EQ(3U, top_three.size());
  iter = top_three.begin();
  EXPECT_EQ(72, iter->count);
  EXPECT_EQ(stack3_, iter->value.call_stack());
  ++iter;
  EXPECT_EQ(64, iter->count);
  EXPECT_EQ(stack2_, iter->value.call_stack());
  ++iter;
  EXPECT_EQ(60, iter->count);
  EXPECT_EQ(stack0_, iter->value.call_stack());

  // Get the top two call sites ordered from least to greatest number of
  // entries.
  RankedSet top_two(2);
  table.GetTopCallStacks(&top_two);
  ASSERT_EQ(2U, top_two.size());
  iter = top_two.begin();
  EXPECT_EQ(72, iter->count);
  EXPECT_EQ(stack3_, iter->value.call_stack());
  ++iter;
  EXPECT_EQ(64, iter->count);
  EXPECT_EQ(stack2_, iter->value.call_stack());
}

}  // namespace leak_detector
}  // namespace metrics
