// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <vector>

namespace chap {
template <typename Offset>
class Unmangler {
 public:
  Unmangler(const char* mangled, bool warnOnFailure)
      : _mangled(mangled),
        _warnOnFailure(warnOnFailure),
        _checkAnonymousNamespace(false) {
    for (const char* pC = mangled; *pC != '\000'; ++pC) {
      if (*pC < '\041' || *pC > '\176') {
        _warnOnFailure = false;
        return;
      }
    }
    Unmangle();
  }
  const std::string& Unmangled() const { return _unmangledName; }

 private:
  const char* _mangled;
  bool _warnOnFailure;
  bool _checkAnonymousNamespace;
  std::vector<char> _unmangled;
  struct PrefixAndSuffix {
    PrefixAndSuffix(size_t prefixBase, size_t prefixLimit, size_t suffixBase,
                    size_t suffixLimit)
        : _prefixBase(prefixBase),
          _prefixLimit(prefixLimit),
          _suffixBase(suffixBase),
          _suffixLimit(suffixLimit) {}
    size_t _prefixBase;
    size_t _prefixLimit;
    size_t _suffixBase;
    size_t _suffixLimit;
  };
  std::vector<PrefixAndSuffix> _names;
  std::string _unmangledName;

  void ReportFailureIfNeeded(const char* remainder) {
    if (_warnOnFailure) {
      std::cerr << "Failed to unmangle \"" << std::string(_mangled)
                << "\"\nremainder: \"" << std::string(remainder) << "\"\n";
      _unmangled.push_back('\000');
      std::string partial(&_unmangled[0]);
      std::cerr << "Partial: \"" << partial << "\"\n";
      if (!_names.empty()) {
        for (size_t i = 0; i < _names.size(); i++) {
          std::cerr << std::dec << i << ": "
                    << "\""
                    << partial.substr(
                           _names[i]._prefixBase,
                           _names[i]._prefixLimit - _names[i]._prefixBase)
                    << "\"\n";
        }
      }
    }
  }
  void Unmangle() {
    if (_mangled[0] != '\000') {
      try {
        const char* pC = _mangled;
        if (*pC == '*') {
          _checkAnonymousNamespace = true;
          pC++;
        }
        const char* remainder = UnmangleOneItem(pC);
        if (*remainder == '\000') {
          _unmangled.push_back('\000');
          _unmangledName.assign(&_unmangled[0]);
        } else {
          ReportFailureIfNeeded(remainder);
        }
      } catch (const char* remainder) {
        ReportFailureIfNeeded(remainder);
      }
    }
  }
  void Append(const char oneToAppend) { _unmangled.push_back(oneToAppend); }
  void Append(const char* toAppend) {
    for (char c = *toAppend++; c != '\000'; c = *toAppend++) {
      _unmangled.push_back(c);
    }
  }
  void AppendFromMangled(const char* toAppend, size_t numChars) {
    for (size_t i = 0; i < numChars; i++) {
      char c = *toAppend++;
      if (c == '\0') {
        throw toAppend;
      }
      _unmangled.push_back(c);
    }
  }
  size_t PushPrefix(size_t unmangledBase) {
    size_t index = _names.size();
    size_t limit = _unmangled.size();
    _names.emplace_back(unmangledBase, limit, limit, limit);
    return index;
  }
  void SetSuffix(size_t index, size_t unmangledBase) {
    PrefixAndSuffix& prefixAndSuffix = _names[index];
    prefixAndSuffix._suffixBase = unmangledBase;
    prefixAndSuffix._suffixLimit = _unmangled.size();
  }

  void UnmangleQualifiers(const char* startQualifiers,
                          const char* endQualifiers, size_t unmangledBase) {
    for (const char* pQualifier = endQualifiers;
         --pQualifier >= startQualifiers;) {
      char qualifier = *pQualifier;
      if (qualifier == 'K') {
        Append(" const");
      } else if (qualifier == 'R') {
        Append('&');
      } else {
        Append('*');
      }
      PushPrefix(unmangledBase);
    }
  }
  const char* UnmangleOneItem(const char* base) {
    const char* unqualifiedBase = base;
    char c = *unqualifiedBase;
    while (c == 'K' || c == 'R' || c == 'P') {
      unqualifiedBase++;
      c = *unqualifiedBase;
    }
    if (c == 'F') {
      return UnmangleFItem(base, unqualifiedBase);
    }
    if (c == 'S') {
      return UnmangleSItem(base, unqualifiedBase);
    }
    size_t unmangledBase = _unmangled.size();
    const char* remainder = UnmangleUnqualifiedItem(unqualifiedBase);
    UnmangleQualifiers(base, unqualifiedBase, unmangledBase);
    return remainder;
  }

  const char* UnmangleFItem(const char* base, const char* unqualifiedBase) {
    size_t prefixBase = _unmangled.size();
    const char* pC = UnmangleOneItem(unqualifiedBase + 1);
    Append('(');
    size_t firstName = PushPrefix(prefixBase);
    size_t lastName = firstName;
    for (const char* pQualifier = unqualifiedBase; --pQualifier >= base;) {
      char qualifier = *pQualifier;
      if (qualifier == 'K') {
        Append(" const");
      } else if (qualifier == 'R') {
        Append('&');
      } else {
        Append('*');
      }
      lastName = PushPrefix(prefixBase);
    }
    size_t suffixBase = _unmangled.size();
    Append(")(");
    bool needComma = false;
    while (*pC != 'E') {
      if (needComma) {
        Append(',');
      } else {
        needComma = true;
      }
      pC = UnmangleOneItem(pC);
    }
    Append(')');
    for (size_t nameIndex = firstName; nameIndex <= lastName; ++nameIndex) {
      SetSuffix(nameIndex, suffixBase);
    }
    return pC + 1;
  }
  const char* UnmangleUnqualifiedItem(const char* base) {
    switch (*base) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        return UnmangleOuterName(base);
      case 'N':
        return UnmangleNItem(base + 1);
      case 'L':
        return UnmangleLItem(base + 1);
      case 'a':
        Append("signed char");
        return base + 1;
      case 'b':
        Append("bool");
        return base + 1;
      case 'c':
        Append("char");
        return base + 1;
      case 'd':
        Append("double");
        return base + 1;
      case 'e':
        Append("long double");
        return base + 1;
      case 'f':
        Append("float");
        return base + 1;
      case 'g':
        Append("__float128");
        return base + 1;
      case 'h':
        Append("unsigned char");
        return base + 1;
      case 'i':
        Append("int");
        return base + 1;
      case 'j':
        Append("unsigned int");
        return base + 1;
      case 'l':
        Append("long");
        return base + 1;
      case 'm':
        Append("unsigned long");
        return base + 1;
      case 'n':
        Append("__int128");
        return base + 1;
      case 'o':
        Append("unsigned __int128");
        return base + 1;
      case 's':
        Append("short");
        return base + 1;
      case 't':
        Append("unsigned short");
        return base + 1;
      case 'u':
        Append("unsigned long long");
        return base + 1;
      case 'v':
        Append("void");
        return base + 1;
      case 'w':
        Append("wchar_t");
        return base + 1;
      case 'x':
        Append("long long");
        return base + 1;
      case 'y':
        Append("unsigned long long");
        return base + 1;
      case 'z':
        Append("...");
        return base + 1;
      default:
        throw base;
    }
  }
  const char* UnmangleOuterName(const char* base) {
    size_t unmangledBase = _unmangled.size();
    const char* nameEnd = UnmangleNameWithLength(base);
    PushPrefix(unmangledBase);
    return UnmangleTemplateArgumentsIfPresent(nameEnd, unmangledBase);
  }
  const char* UnmangleNItem(const char* base) {
    size_t unmangledBase = _unmangled.size();
    const char* pC = base;
    for (char c = *pC; c != 'E'; c = *pC) {
      if (c == 'S') {
        if (pC != base) {
          return pC;
        }
        pC = UnmangleSItem(pC, pC);
      } else if (c >= '0' && c <= '9') {
        if (pC != base) {
          Append("::");
        }
        pC = UnmangleNameWithLength(pC);
        PushPrefix(unmangledBase);
      } else if (c == 'I') {
        pC = UnmangleTemplateArgumentsIfPresent(pC, unmangledBase);
      } else {
        throw pC;
      }
    }
    return pC + 1;
  }

  const char* UnmangleSItem(const char* base, const char* unqualifiedBase) {
    size_t unmangledBase = _unmangled.size();
    const char* pC = unqualifiedBase + 1;
    bool suffixApplied = false;
    bool allowExtend = true;
    char c = *pC;
    if (c == 't') {
      Append("std");
      pC++;
    } else if (c == 's') {
      Append("std::string");
      pC++;
      allowExtend = false;
    } else if (c == 'a') {
      Append("std::allocator");
      pC++;
    } else {
      size_t index = 0;
      if (c >= '0' && c <= '9') {
        index = 1 + (c - '0');
        c = *(++pC);
      } else if (c >= 'A' && c <= 'Z') {
        index = 11 + (c - 'A');
        c = *(++pC);
      }
      if (c != '_') {
        throw pC;
      }
      if (index >= _names.size()) {
        throw pC;
      }
      PrefixAndSuffix& prefixAndSuffix = _names[index];

      size_t offsetInUnmangled = prefixAndSuffix._prefixBase;
      size_t limitInUnmangled = prefixAndSuffix._prefixLimit;
      while (offsetInUnmangled < limitInUnmangled) {
        _unmangled.push_back(_unmangled[offsetInUnmangled++]);
      }
      if (prefixAndSuffix._suffixBase != prefixAndSuffix._suffixLimit) {
        size_t lastName = 0;
        for (const char* pQualifier = unqualifiedBase; --pQualifier >= base;) {
          char qualifier = *pQualifier;
          if (qualifier == 'K') {
            Append(" const");
          } else if (qualifier == 'R') {
            Append('&');
          } else {
            Append('*');
          }
          lastName = PushPrefix(unmangledBase);
        }
        size_t suffixBase = _unmangled.size();
        offsetInUnmangled = prefixAndSuffix._suffixBase;
        limitInUnmangled = prefixAndSuffix._suffixLimit;
        while (offsetInUnmangled < limitInUnmangled) {
          _unmangled.push_back(_unmangled[offsetInUnmangled++]);
        }
        for (const char* pQualifier = base; pQualifier++ < unqualifiedBase;) {
          SetSuffix(lastName--, suffixBase);
        }
        suffixApplied = true;
      }
      ++pC;
    }
    if (allowExtend) {
      for (char c = *pC; c >= '0' && c <= '9'; c = *pC) {
        Append("::");
        pC = UnmangleNameWithLength(pC);
        PushPrefix(unmangledBase);
      }
      pC = UnmangleTemplateArgumentsIfPresent(pC, unmangledBase);
    }
    if (!suffixApplied) {
      UnmangleQualifiers(base, unqualifiedBase, unmangledBase);
    }
    return pC;
  }
  const char* UnmangleTemplateArgumentsIfPresent(const char* base,
                                                 size_t unmangledBase) {
    const char* pC = base;
    if (*base == 'I') {
      pC++;
      Append('<');
      bool firstTime = true;
      while (*pC != 'E') {
        if (firstTime) {
          firstTime = false;
        } else {
          Append(",");
        }
        pC = UnmangleOneItem(pC);
      }
      Append('>');
      PushPrefix(unmangledBase);
      pC++;
    }
    return pC;
  }
  const char* UnmangleLItem(const char* base) {
    const char* pC = base;
    for (char c = *pC; c != 'E'; c = *pC) {
      if (c == 'b') {
        c = *++pC;
        if (c == '0') {
          Append("false");
        } else if (c == '1') {
          Append("true");
        } else {
          throw base;
        }
        pC++;
      } else if (c == 'N') {
        Append("(");
        pC = UnmangleNItem(pC + 1);
        Append(")");
      } else if (c >= '0' && c <= '9') {
        Append(c);
        pC++;
      } else {
        // This is proably some kind of literal that we do not yet support.
        throw base;
      }
    }
    return pC + 1;
  }
  const char* UnmangleNameWithLength(const char* base) {
    int length = *base - '0';
    const char* pC = base;
    for (char c = *(++pC); (c >= '0') && (c <= '9'); c = *(++pC)) {
      length = (length * 10) + (c - '0');
    }
    if (_checkAnonymousNamespace && std::string(pC, length) == "_GLOBAL__N_1") {
      Append("(anonymous)");
    } else {
      AppendFromMangled(pC, length);
    }
    return pC + length;
  }
};
}  // namespace chap
