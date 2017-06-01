#include <string>
#include <vector>
int main(int, char **, char **) {
  std::vector<std::string> v;
  v.push_back("some useless string");
  v.push_back("another useless string");
  v.pop_back();
  *((int *)(0)) = 92;
}
