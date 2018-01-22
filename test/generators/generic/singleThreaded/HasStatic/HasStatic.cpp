#include <map>
#include <string>
using namespace std;
map<std::string, int> SomeBogusMap;
int main(int, char **, char **) {

  SomeBogusMap["some bogus key"] = 92;
  SomeBogusMap["another bogus key"] = 93;
  *((int *)(0)) = 92;
}
