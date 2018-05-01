#include <map>
#include <string>
using namespace std;
map<std::string, int> SomeBogusMap;
std::string staticString;
int main(int, char **, char **) {
  /*
   * This is used for testing the LongString pattern recognizer, which
   * recognizes, for recent implentations of std::string, when an allocation
   * is being used for an external string buffer.
   * One key thing here, given that recognition is based on the location of
   * the std::string itself, that we have instances of std::string on the
   * stack, statically anchored, and in allocations.
   */
  SomeBogusMap["some bogus key"] = 92;
  SomeBogusMap["another bogus key"] = 93;
  std::string s1("0123456789abcdef");
  std::string s2("0123456789abcdefg");
  staticString = "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz";
  s2.resize(4);
  
  *((int *)(0)) = 92;
}
