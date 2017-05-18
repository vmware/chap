class B {
public:
  B() {}
  virtual int f() { return 5; }
};
class D {
public:
  D(){}
  virtual int f() { return 92; }
};
int main(int, char **, char **) {
  B *b = new B();
  D *d = new D();
  *((int *)(0)) = 92;
}
