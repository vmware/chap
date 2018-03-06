#include <thread>
#include <vector>
static std::vector<int> staticVector;
void f() {
   for (int i = 0; i < 100000000; i++)
      for (auto expect92 : staticVector)
         if (expect92 != 92) *((int *)(0)) = expect92;
}
int main(int argc, char **argv) {
   staticVector.push_back(92);
   std::thread t(&f);
   for (int i = 0; i < 100000000; i++) {
      std::vector<int> v;
      v.resize(i & 0x1f, 92);
      staticVector.swap(v);
   }
   t.join();
   return 0;
}
