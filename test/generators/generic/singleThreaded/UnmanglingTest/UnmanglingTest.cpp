#include <iostream>
#include <map>
#include <set>
#include <vector>
namespace NSA {
struct HasVirtualDestructor {
  virtual ~HasVirtualDestructor() {}
};
namespace NSB {
struct SA : public HasVirtualDestructor {
  ~SA() {}
  int a;
};
} /* NSB */
namespace NSC {
template <typename T1, typename T2, typename T3, typename T4, typename T5,
          typename T6, typename T7, typename T8, typename T9, typename T10,
          typename T11, typename T12>
struct Dozen : public HasVirtualDestructor {
  ~Dozen() {}
  T1 t1;
  T2 t2;
  T3 t3;
  T4 t4;
  T5 t5;
  T6 t6;
  T7 t7;
  T8 t8;
  T9 t9;
  T10 t10;
  T11 t11;
  T12 t12;
  struct FirstTwo : public HasVirtualDestructor {
    ~FirstTwo() {}
    T1 t1;
    T2 t2;
  };
};
}  // end NSC
struct SB : public HasVirtualDestructor {
  ~SB() {}
  int a;
};
}  // end NSA
int main(int argc, char **argv) {
  std::vector<NSA::HasVirtualDestructor *> anchored;
  anchored.push_back(
      new NSA::NSC::Dozen<signed char, char, unsigned char, int, unsigned int,
                          short, unsigned short, long, unsigned long, double,
                          long double, float>());
  anchored.push_back(
      new NSA::NSC::Dozen<signed char, char, unsigned char, int, unsigned int,
                          short, unsigned short, long, unsigned long, double,
                          long double, float>::FirstTwo());

  anchored.push_back(
      new NSA::NSC::Dozen<bool, long long, unsigned long long, long double,
                          __float128, void ***, void **, void *, wchar_t,
                          std::map<int, long>, std::set<int>, std::string>());

  anchored.push_back(
      new NSA::NSC::Dozen<
          void (*)(), bool (*)(char), char (*)(short, long), void (*)(int, ...),
          const std::string &(*)(int, short), int (**)(),
          char (**)(short, long), NSA::SB *, NSA::NSB::SA, NSA::NSB::SA &(*)(),
          const NSA::SB &(*)(), char (***)(short, long)>());

  anchored.push_back(
      new NSA::NSC::Dozen<void (*)(void), void (*)(int), void (*)(int, short),
                          int (*)(void), int (*)(int), int (*)(int, short),
                          short (**)(int, short, long), int, int, int, int,
                          int>());

  *((int *)(0)) = 92;
}
