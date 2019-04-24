#include <list>
#include <string>
using namespace std;
struct S1 {
   S1(int base) {
     _l1.push_back(base + 0);
     _l2.push_back("lkjsadflkjds");
     _l3.push_back(base + 0x10);
     _l3.push_back(base + 0x11);
     _l3.push_back(base + 0x12);
   }
   list<int> _l1;
   list<string> _l2;
   list<char> _l3;
};

struct S2 {
   S2(int base, int limit) : _i(base * 0x100) {
     for (int i = base; i < limit; i++) {
       _l.push_back(i);
     }
   }
   list<int> _l;
   int _i;
};

static S1 s1static(0x90);
static S2 s2static(0xa0, 0xb0);

int main(int, char **, char **) {
  list<int> l1;
  
  list<int> l2;
  l2.push_back(0);
  
  list<unsigned long long> l3;
  l3.push_back(0x10);
  l3.push_back(0x11);
  
  list<unsigned long long> l4;
  l4.push_back(0x20);
  l4.push_back(0x21);
  l4.push_back(0x22);

  S1 *pS1 = new S1(0x30);
  S1 s1(0x50);

  S2 s2(0x50, 0x70);
  S2 *pS2 = new S2(0x70,0x78);
  S2 *pS2b = new S2(0x78, 0x79);
  *((int *)(0)) = 92;
}
