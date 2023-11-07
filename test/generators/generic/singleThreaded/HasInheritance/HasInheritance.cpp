#include <vector>
using namespace std;
struct S1 {
   S1(int v) : _v(v) {}
   virtual ~S1() {}
   int _v;
   virtual int f(int i) { return _v * i; }
};

struct S2 : public S1 {
   S2(int w) : S1(w >> 1), _w(w) {}
   int _w;
   virtual int f(int i) { return _w + i * _v; }
};

struct S3 : public S1 {
   S3(int w) :S1(w >> 1), _w(w) {}
   int _w;
   virtual int f(int i) { return _w ^ i * _v; }
};

struct S4 : public S2 {
   S4(int x) : S2(x * 11), _x(x) {}
   int _x;
   virtual int f(int i) { return _x & i; }
};

int main(int, char **, char**) {
  vector<S1 *> v;
  v.push_back(new S1(37));
  v.push_back(new S2(41));
  v.push_back(new S3(61));
  v.push_back(new S4(97));
  *((int *)(0)) = 92;
}
