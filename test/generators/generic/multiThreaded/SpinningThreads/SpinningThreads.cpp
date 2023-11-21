/*
 * This is intended to test a case where there are multiple threads that
 * are calling malloc around the same time.  It is useful for verifying
 * that the various stacks are found and that the data structures used
 * to avoid allocation collisions are found properly.  Run as compiled,
 * it also can be used to see some interesting things about stack
 * usage.
 * Here is a sample command line to compile it:
 * g++ -pthread -o SpinningThreads -O9 --std=c++11 SpinningThreads.cpp
 */

#include <list>
#include <thread>
using namespace std;
struct Spinner {
  Spinner(unsigned long long numSpins, unsigned long long firstValue,
          unsigned long long skipBy)
      : _numSpins(numSpins), _firstValue(firstValue), _skipBy(skipBy) {}
  void operator()() {
    unsigned long long nextValue = _firstValue;
    for (unsigned long long spinsLeft = _numSpins; spinsLeft > 0; --spinsLeft) {
      list<unsigned long long> l;
      l.push_back(nextValue);
      nextValue += _skipBy;
    }
  }
  unsigned long long _numSpins;
  unsigned long long _firstValue;
  unsigned long long _skipBy;
};

int main(int argc, char **argv) {
  Spinner longSpinner1(~0, 1, 0x10);
  Spinner longSpinner2(~0, 2, 0x10);
  Spinner mediumSpinner(0x1000000, 3, 0x10);
  Spinner shortSpinner(0x10000, 4, 0x10);
  std::thread t1(longSpinner1);
  std::thread t2(longSpinner2);
  std::thread t3(shortSpinner);
  std::thread t4(mediumSpinner);
  t4.join();
  *((int *)(0)) = 92;
  t3.join();
  return 0;
}
