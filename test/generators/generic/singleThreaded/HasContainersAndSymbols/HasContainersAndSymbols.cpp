#include <iostream>
#include <list>
#include <set>
#include <deque>
#include <vector>

class HasContainer {
 public:
  HasContainer() {}
   virtual size_t Count(size_t depth) const = 0;
};

class HasSet : public HasContainer {
 public:
  HasSet() {}
  size_t Count(size_t depth) const {
    size_t count = 1;
    if (depth > 0) {
      for (std::set<HasContainer *>::const_iterator it = _s.begin();
           it != _s.end(); ++it) {
        count += (*it)->Count(depth - 1);
      }
    }
    return count;
  }
  void Add(HasContainer *inner) { _s.insert(inner); }

 private:
  std::set<HasContainer *> _s;
};

class HasList : public HasContainer {
 public:
  HasList() {}
  size_t Count(size_t depth) const {
    size_t count = 1;
    if (depth > 0) {
      for (std::list<HasContainer *>::const_iterator it = _l.begin();
           it != _l.end(); ++it) {
        count += (*it)->Count(depth - 1);
      }
    }
    return count;
  }
  void Add(HasContainer *inner) { _l.push_back(inner); }

 private:
  std::list<HasContainer *> _l;
};

class HasVector : public HasContainer {
 public:
  HasVector() {}
  size_t Count(size_t depth) const {
    size_t count = 1;
    if (depth > 0) {
      for (std::vector<HasContainer *>::const_iterator it = _v.begin();
           it != _v.end(); ++it) {
        count += (*it)->Count(depth - 1);
      }
    }
    return count;
  }
  void Add(HasContainer *inner) { _v.push_back(inner); }

 private:
  std::vector<HasContainer *> _v;
};

class HasDeque : public HasContainer {
 public:
  HasDeque() {}
  size_t Count(size_t depth) const {
    size_t count = 1;
    if (depth > 0) {
      for (std::deque<HasContainer *>::const_iterator it = _d.begin();
           it != _d.end(); ++it) {
        count += (*it)->Count(depth - 1);
      }
    }
    return count;
  }
  void Add(HasContainer *inner) { _d.push_back(inner); }

 private:
  std::deque<HasContainer *> _d;
};

class HasPair : public HasContainer {
 public:
  HasPair(HasContainer *first, HasContainer *second) {
    _p.first = first;
    _p.second = second;
  }
  size_t Count(size_t depth) const {
    size_t count = 1;
    if (depth > 0) {
      if (_p.first != 0) {
        count += (_p.first)->Count(depth - 1);
      }
      if (_p.second != 0) {
        count += (_p.second)->Count(depth - 1);
      }
    }
    return count;
  }

 private:
  std::pair<HasContainer *, HasContainer *> _p;
};

int main(int, char **, char **) {
  /*
   * Make some spaghetti to give lots of ways to test various commands and
   * switches.
   */
  HasSet *hasSet = new HasSet();
  HasList *hasList = new HasList();
  HasDeque *hasDeque = new HasDeque();
  HasVector *hasVector = new HasVector();
  hasSet->Add(new HasList());
  hasSet->Add(hasList);
  hasDeque->Add(hasList);
  hasDeque->Add(hasVector);
  hasSet->Add(hasDeque);
  HasPair *hasPair = new HasPair(hasSet, new HasList());
  for (size_t depth = 0; depth < 5; depth++) {
    std::cout << "Depth " << depth << " count " << hasPair->Count(depth)
              << std::endl;
  }
  *((int *)(0)) = 92;
}
