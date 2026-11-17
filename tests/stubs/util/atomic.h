#ifndef TEST_ATOMIC_H
#define TEST_ATOMIC_H

extern bool testInterruptsEnabled;

struct TestAtomicBlock {
  bool saved = testInterruptsEnabled;
  bool active = true;
  TestAtomicBlock() { testInterruptsEnabled = false; }
  ~TestAtomicBlock() { testInterruptsEnabled = saved; }
};

#define ATOMIC_RESTORESTATE 0
#define ATOMIC_BLOCK(type) for (TestAtomicBlock block; block.active; block.active = false)

#endif
