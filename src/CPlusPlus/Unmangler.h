// Copyright (c) 2017,2023,2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include <vector>

namespace chap {
namespace CPlusPlus {
template <typename Offset>
class Unmangler {
 public:
  Unmangler(const char* mangled, bool warnOnFailure)
      : _mangled(mangled),
        _mangledLimit(mangled + strlen(mangled)),
        _warnOnFailure(warnOnFailure),
        _checkAnonymousNamespace(false),
        _topTemplateContext(nullptr) {
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
  const char* _mangledLimit;
  bool _warnOnFailure;
  bool _checkAnonymousNamespace;
  std::string _unmangledName;

  struct UnmangledItem {
    UnmangledItem()
        : _parenthesizedExtensionPoint(std::string::npos),
          _hasKQualifier(false),
          _hasVQualifier(false),
          _hasRQualifier(false),
          _hasOQualifier(false) {}
    UnmangledItem(const UnmangledItem& other) {
      _unmangled = other._unmangled;
      _parenthesizedExtensionPoint = other._parenthesizedExtensionPoint;
      _hasKQualifier = other._hasKQualifier;
      _hasVQualifier = other._hasVQualifier;
      _hasRQualifier = other._hasRQualifier;
      _hasOQualifier = other._hasOQualifier;
    }
    UnmangledItem& operator=(const UnmangledItem&) = default;
    void Extend(std::string toAdd, bool addParenthesesIfApplicable) {
      if (_unmangled.empty()) {
        return;
      }
      if (!_packMembers.empty()) {
        _unmangled.clear();
        bool needsComma = false;
        for (auto& packMember : _packMembers) {
          if (needsComma) {
            _unmangled.append(",");
          } else {
            needsComma = true;
          }
          packMember.Extend(toAdd, addParenthesesIfApplicable);
          _unmangled.append(packMember._unmangled);
        }
        return;
      }

      if (_parenthesizedExtensionPoint != std::string::npos) {
        if (_unmangled[_parenthesizedExtensionPoint] == ')') {
          _unmangled.insert(_parenthesizedExtensionPoint, toAdd);
          _parenthesizedExtensionPoint += toAdd.size();
        } else {
          if (addParenthesesIfApplicable) {
            std::string beforeExtensionPoint =
                _unmangled.substr(0, _parenthesizedExtensionPoint);
            std::string afterExtensionPoint =
                _unmangled.substr(_parenthesizedExtensionPoint);
            _unmangled =
                beforeExtensionPoint + "(" + toAdd + ")" + afterExtensionPoint;
            _parenthesizedExtensionPoint += (1 + toAdd.size());
          } else {
            _unmangled.append(toAdd);
          }
        }
        return;
      }
      size_t unmangledSize = _unmangled.size();
      if (unmangledSize > 3 && _unmangled[unmangledSize - 3] == '.' &&
          _unmangled[unmangledSize - 2] == '.' &&
          _unmangled[unmangledSize - 1] == '.') {
        _unmangled.insert(unmangledSize - 3, toAdd);
        return;
      }
      _unmangled.append(toAdd);
    }
    bool HasUnusedParenthesizedExtensionPoint() {
      return _parenthesizedExtensionPoint != std::string::npos &&
             _unmangled[_parenthesizedExtensionPoint] != ')';
    }
    std::string _unmangled;
    std::string::size_type _parenthesizedExtensionPoint;
    std::list<UnmangledItem> _packMembers;
    bool _hasKQualifier;
    bool _hasVQualifier;
    bool _hasRQualifier;
    bool _hasOQualifier;
  };
  std::vector<UnmangledItem> _sValues;

  struct TemplateContext {
    TemplateContext(struct TemplateContext** ppTopContext,
                    const char* remainder)
        : _resolved(false),
          _ppTopContext(ppTopContext),
          _prevContext(*ppTopContext),
          _remainder(remainder) {
      *ppTopContext = this;
    }
    ~TemplateContext() { *_ppTopContext = _prevContext; }
    virtual void ShowItems() const {
      for (const UnmangledItem& templateArgument : _templateArguments) {
        std::cerr << "\"" << templateArgument._unmangled << "\"\n";
      }
    }
    virtual bool AddItem(const UnmangledItem& item) {
      _templateArguments.push_back(item);
      return true;
    }
    virtual bool GetItem(size_t index, UnmangledItem& item) const {
      if (_resolved) {
        if (index < _templateArguments.size()) {
          item = _templateArguments[index];
          return true;
        }
        return false;
      }
      return (_prevContext != nullptr) && _prevContext->GetItem(index, item);
    }
    virtual void ClearTemplateArguments() {
      _resolved = false;
      _templateArguments.clear();
    }
    bool IsResolved() const { return _resolved; }
    void Resolve() { _resolved = true; }
    bool IsInDp() const { return _isInDp; }
    void SetInDp(bool isInDp) { _isInDp = isInDp; }

    bool _resolved;
    bool _isInDp;
    std::vector<UnmangledItem> _templateArguments;
    struct TemplateContext** const _ppTopContext;
    struct TemplateContext* const _prevContext;
    const char* const _remainder;
  };

  struct LambdaTemplateContext : public TemplateContext {
    LambdaTemplateContext(struct TemplateContext** ppTopContext,
                          const char* remainder)
        : TemplateContext(ppTopContext, remainder) {
      TemplateContext::_resolved = true;
    }
    virtual void ShowItems() const {
      std::cerr << "This is a lambda template context with auto: items.\n";
    }
    virtual bool AddItem(const UnmangledItem& item) { return false; }
    virtual bool GetItem(size_t index, UnmangledItem& item) const {
      if (TemplateContext::_isInDp) {
        item._unmangled.append("auto...");
        return true;
      }
      item._unmangled.append("auto:");
      index++;
      return index != 0 && AppendIndex(index, item);
    }
  };
  struct TemplateContext* _topTemplateContext;

  void ShowTemplateContextStack() {
    if (_topTemplateContext != nullptr) {
      std::cerr << "TemplateContext stack from top down:\n";
      for (TemplateContext* templateContext = _topTemplateContext;
           templateContext != nullptr;
           templateContext = templateContext->_prevContext) {
        std::cerr << (templateContext->IsResolved() ? "Resolved "
                                                    : "Unresolved ")
                  << "TemplateContext at 0x" << std::hex << templateContext
                  << " started with remainder \"" << templateContext->_remainder
                  << "\"\n";
        templateContext->ShowItems();
      }
    } else {
      std::cerr << "Template context stack is empty.\n";
    }
  }

  void ReportFailureIfNeeded(const char* remainder) {
    if (_warnOnFailure) {
      std::cerr << "Failed to unmangle \"" << std::string(_mangled)
                << "\"\nRemainder: \"" << std::string(remainder) << "\"\n";
      if (!_sValues.empty()) {
        std::cerr << "S values:\n";
        for (const UnmangledItem& sValue : _sValues) {
          std::cerr << "\"" << sValue._unmangled << "\"\n";
        }
      }
      if (_topTemplateContext != nullptr) {
        std::cerr << "Template stack from top down:\n";
        for (TemplateContext* templateContext = _topTemplateContext;
             templateContext != nullptr;
             templateContext = templateContext->_prevContext) {
          std::cerr << "Context at 0x" << std::hex << templateContext
                    << " started with remainder \""
                    << templateContext->_remainder << "\"\n";
          for (const UnmangledItem& templateArgument :
               templateContext->_templateArguments) {
            std::cerr << templateArgument._unmangled;
          }
        }
      }
    }
  }
  void Unmangle() {
    UnmangledItem topItem;
    if (_mangled[0] != '\000') {
      try {
        const char* pC = _mangled;
        if (*pC == '*') {
          _checkAnonymousNamespace = true;
          pC++;
        }
        const char* remainder = UnmangleOneItem(pC, topItem);
        if (remainder == _mangledLimit) {
          _unmangledName = topItem._unmangled;
        } else {
          if (remainder > _mangledLimit) {
            remainder = _mangledLimit;
          }
          ReportFailureIfNeeded(remainder);
        }
      } catch (const char* remainder) {
        ReportFailureIfNeeded(remainder);
      }
    }
  }

  const char* UnmangleOneItem(const char* base, UnmangledItem& topItem) {
    switch (base[0]) {
      case 'B':
        return UnmangleBItem(base, topItem);
      case 'D':
        switch (base[1]) {
          case 'a':
            topItem._unmangled.assign("auto");
            return base + 2;
          case 'c':
            topItem._unmangled.assign("decltype(auto)");
            return base + 2;
          case 'i':
            topItem._unmangled.assign("char32_t");
            return base + 2;
          case 'o':
            return UnmangleDoItem(base, topItem);
          case 'n':
            topItem._unmangled.assign("decltype(nullptr)");
            return base + 2;
          case 'p':
            return UnmangleDpItem(base, topItem);
          case 's':
            topItem._unmangled.assign("char16_t");
            return base + 2;
          case 'u':
            topItem._unmangled.assign("char8_t");
            return base + 2;
          default:
            throw base;
        }
      case 'F':
        return UnmangleFItem(base, topItem);
      case 'K':
        return UnmangleKItem(base, topItem);
      case 'L':
        return UnmangleLItem(base, topItem);
      case 'M':
        return UnmangleMItem(base, topItem);
      case 'N':
        return UnmangleNItem(base, topItem);
      case 'O':
        return UnmangleOItem(base, topItem);
      case 'P':
        return UnmanglePItem(base, topItem);
      case 'R':
        return UnmangleRItem(base, topItem);
      case 'S':
        return UnmangleSItem(base, topItem);
      case 'T':
        return UnmangleTItem(base, topItem);
      case 'U':
        return UnmangleUItem(base, topItem);
      case 'X':
        return UnmangleXItem(base, topItem);
      case 'Z':
        return UnmangleZItem(base, topItem);
      case '_':
        if (base[1] == 'Z') {
          return Unmangle_ZItem(base, topItem);
        }
        throw base;
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
        return UnmangleSingleNameWithOptionalTemplateArguments(base, topItem);
      case 'a':
        topItem._unmangled.assign("signed_char");
        return base + 1;
      case 'b':
        topItem._unmangled.assign("bool");
        return base + 1;
      case 'c':
        topItem._unmangled.assign("char");
        return base + 1;
      case 'd':
        topItem._unmangled.assign("double");
        return base + 1;
      case 'e':
        topItem._unmangled.assign("long_double");
        return base + 1;
      case 'f':
        topItem._unmangled.assign("float");
        return base + 1;
      case 'g':
        topItem._unmangled.assign("__float128");
        return base + 1;
      case 'h':
        topItem._unmangled.assign("unsigned_char");
        return base + 1;
      case 'i':
        topItem._unmangled.assign("int");
        return base + 1;
      case 'j':
        topItem._unmangled.assign("unsigned_int");
        return base + 1;
      case 'l':
        topItem._unmangled.assign("long");
        return base + 1;
      case 'm':
        topItem._unmangled.assign("unsigned_long");
        return base + 1;
      case 'n':
        topItem._unmangled.assign("__int128");
        return base + 1;
      case 'o':
        topItem._unmangled.assign("unsigned___int128");
        return base + 1;
      case 's':
        topItem._unmangled.assign("short");
        return base + 1;
      case 't':
        topItem._unmangled.assign("unsigned_short");
        return base + 1;
      case 'u':
        topItem._unmangled.assign("unsigned_long_long");
        return base + 1;
      case 'v':
        topItem._unmangled.assign("void");
        return base + 1;
      case 'w':
        topItem._unmangled.assign("wchar_t");
        return base + 1;
      case 'x':
        topItem._unmangled.assign("long_long");
        return base + 1;
      case 'y':
        topItem._unmangled.assign("unsigned_long_long");
        return base + 1;
      case 'z':
        topItem._unmangled.assign("...");
        return base + 1;
      default:
        throw base;
    }
  }

  const char* UnmangleBItem(const char* base, UnmangledItem& bItem) {
    UnmangledItem argItem;
    const char* pC = UnmangleOneItem(base + 1, argItem);
    bItem._unmangled.append("[abi:");
    bItem._unmangled.append(argItem._unmangled);
    bItem._unmangled.append("]");
    return pC;
  }

  const char* UnmangleKItem(const char* base, UnmangledItem& kItem) {
    UnmangledItem unqualifiedItem;
    const char* pC = UnmangleOneItem(base + 1, unqualifiedItem);
    kItem = unqualifiedItem;
    kItem.Extend("_const", false);
    if (unqualifiedItem.HasUnusedParenthesizedExtensionPoint()) {
      _sValues.back() = kItem;
    } else {
      _sValues.push_back(kItem);
    }
    return pC;
  }
  const char* UnmangleDoItem(const char* base, UnmangledItem& doItem) {
    UnmangledItem unqualifiedItem;
    const char* pC = UnmangleOneItem(base + 2, unqualifiedItem);
    doItem = unqualifiedItem;
    doItem.Extend("_noexcept", false);
    if (unqualifiedItem.HasUnusedParenthesizedExtensionPoint()) {
      _sValues.back() = doItem;
    } else {
      _sValues.push_back(doItem);
    }
    return pC;
  }
  const char* UnmangleDpItem(const char* base, UnmangledItem& dpItem) {
    if (_topTemplateContext == nullptr) {
      throw base;
    }
    _topTemplateContext->SetInDp(true);
    const char* pC = UnmangleOneItem(base + 2, dpItem);
    _topTemplateContext->SetInDp(false);
    return pC;
  }

  const char* UnmangleRItem(const char* base, UnmangledItem& rItem) {
    UnmangledItem unqualifiedItem;
    const char* pC = UnmangleOneItem(base + 1, unqualifiedItem);
    rItem = unqualifiedItem;
    rItem.Extend("&", true);
    _sValues.push_back(rItem);
    return pC;
  }

  const char* UnmangleOItem(const char* base, UnmangledItem& pItem) {
    UnmangledItem unqualifiedItem;
    const char* pC = UnmangleOneItem(base + 1, unqualifiedItem);
    pItem = unqualifiedItem;
    pItem.Extend("&&", true);
    _sValues.push_back(pItem);
    return pC;
  }

  const char* UnmanglePItem(const char* base, UnmangledItem& pItem) {
    UnmangledItem unqualifiedItem;
    const char* pC = UnmangleOneItem(base + 1, unqualifiedItem);
    pItem = unqualifiedItem;
    pItem.Extend("*", true);
    _sValues.push_back(pItem);
    return pC;
  }

  const char* UnmangleFItem(const char* base, UnmangledItem& fItem) {
    UnmangledItem returnType;
    TemplateContext templateContext(&_topTemplateContext, base);
    const char* pC = UnmangleOneItem(base + 1, returnType);
    std::vector<UnmangledItem> arguments;
    pC = UnmangleFunctionArguments(pC, arguments);

    fItem._unmangled = returnType._unmangled + "(";
    fItem._parenthesizedExtensionPoint = returnType._unmangled.size();
    if (!arguments.empty()) {
      size_t numArgs = arguments.size();
      fItem._unmangled.append(arguments[0]._unmangled);
      for (size_t argIndex = 1; argIndex < numArgs; ++argIndex) {
        fItem._unmangled.append(",");
        fItem._unmangled.append(arguments[argIndex]._unmangled);
      }
    }
    fItem._unmangled.append(")");
    _sValues.push_back(fItem);
    return pC;
  }

  const char* GetTIndex(const char* base, size_t& index) {
    const char* pC = base + 1;
    index = 0;
    if (*pC != '_') {
      do {
        if (*pC < '0' || *pC > '9') {
          throw base;
        }
        index = index * 10 + (*pC - '0');
      } while (*(++pC) != '_');
      index += 1;
    }
    return pC + 1;
  }

  const char* UnmangleTItem(const char* base, UnmangledItem& tItem) {
    size_t index;
    const char* pC = GetTIndex(base, index);
    if (_topTemplateContext == nullptr ||
        !_topTemplateContext->GetItem(index, tItem)) {
      if (_warnOnFailure) {
        std::cerr << "No template item was available for index " << std::dec
                  << index << "\n";
        ShowTemplateContextStack();
      }
      throw base;
    }
    _sValues.push_back(tItem);
    if (*pC == 'I') {
      pC = AppendTemplateArguments(pC, tItem);
      _sValues.push_back(tItem);
    }
    return pC;
  }

  const char* UnmangleFunctionArguments(const char* pC,
                                        std::vector<UnmangledItem>& arguments) {
    if (*pC == 'v') {
      if (pC[1] != 'E') {
        throw pC;
      }
      return pC + 2;
    }
    while (*pC != 'E') {
      UnmangledItem argItem;
      pC = UnmangleOneItem(pC, argItem);
      arguments.push_back(argItem);
    }
    return pC + 1;
  }

  const char* UnmangleMItem(const char* base, UnmangledItem& mItem) {
    if (base[1] == 'U') {
      return UnmangleUItem(base + 1, mItem);
    }
    UnmangledItem holderType;
    const char* pC = UnmangleOneItem(base + 1, holderType);
    UnmangledItem fItem;
    pC = UnmangleOneItem(pC, fItem);
    mItem = fItem;
    mItem.Extend(holderType._unmangled + "::*", true);
    _sValues.push_back(mItem);
    return pC;
  }

  const char* Unmangle_ZItem(const char* base, UnmangledItem& z_Item) {
    const char* pC = UnmangleOneItem(base + 2, z_Item);
    // Note that the trailing E, if present, is associated with an
    // enclosing L.
    if (*pC == 'E' || *pC == '\000') {
      return pC;
    }
    z_Item._unmangled.append("(");
    do {
      UnmangledItem argItem;
      pC = UnmangleOneItem(pC, argItem);
      z_Item._unmangled.append(argItem._unmangled);
    } while (*pC != 'E' && *pC != '\000');
    z_Item._unmangled.append("(");
    return pC;
  }

  const char* UnmangleZItem(const char* base, UnmangledItem& zItem) {
    TemplateContext templateContext(&_topTemplateContext, base);
    const char* pC =
        (base[1] == 'L')
            ? UnmangleSingleNameWithOptionalTemplateArguments(base + 2, zItem)
            : UnmangleOneItem(base + 1, zItem);
    _sValues.pop_back();  // The function name isn't in the S_ values.
    if (!templateContext._templateArguments.empty()) {
      UnmangledItem returnTypeItem;
      pC = UnmangleOneItem(pC, returnTypeItem);
    }
    std::vector<UnmangledItem> arguments;
    pC = UnmangleFunctionArguments(pC, arguments);

    zItem._unmangled.append("(");
    if (!arguments.empty()) {
      size_t numArgs = arguments.size();
      zItem._unmangled.append(arguments[0]._unmangled);
      for (size_t argIndex = 1; argIndex < numArgs; ++argIndex) {
        zItem._unmangled.append(",");
        zItem._unmangled.append(arguments[argIndex]._unmangled);
      }
    }
    zItem._unmangled.append(")");
    if (zItem._hasKQualifier) {
      zItem._unmangled.append("_const");
    }
    if (zItem._hasVQualifier) {
      zItem._unmangled.append("_volatile");
    }
    if (zItem._hasRQualifier) {
      zItem._unmangled.append("&");
    }
    if (zItem._hasOQualifier) {
      zItem._unmangled.append("&&");
    }
    zItem._unmangled.append("::");
    UnmangledItem localNameItem;
    pC = UnmangleOneItem(pC, localNameItem);
    zItem._unmangled.append(localNameItem._unmangled);
    if (localNameItem._hasKQualifier) {
      zItem._unmangled.append("_const");
    }
    if (localNameItem._hasVQualifier) {
      zItem._unmangled.append("_volatile");
    }
    if (localNameItem._hasRQualifier) {
      zItem._unmangled.append("&");
    }
    if (localNameItem._hasOQualifier) {
      zItem._unmangled.append("&&");
    }
    _sValues.push_back(zItem);
    return pC;
  }

  const char* UnmangleExpression(const char* base, UnmangledItem& eItem) {
    if (base[0] == 'a' && base[1] == 'd') {
      eItem._unmangled.append("&(");
      UnmangledItem argItem;
      const char* pC = UnmangleOneItem(base + 2, argItem);
      eItem._unmangled.append(argItem._unmangled);
      eItem._unmangled.append(")");
      return pC;
    }
    if (base[0] == 's' && base[1] == 'p') {
      return UnmangleOneItem(base + 2, eItem);
    }
    throw base;
  }
  const char* UnmangleXItem(const char* base, UnmangledItem& xItem) {
    const char* pC = UnmangleExpression(base + 1, xItem);
    if (*pC != 'E') {
      throw base;
    }
    return ++pC;
  }

  const char* UnmangleUItem(const char* base, UnmangledItem& uItem) {
    const char* pC = base;
    if (*(pC) != 'U') {
      throw base;
    }
    if (*(++pC) != 'l') {
      throw base;
    }
    LambdaTemplateContext templateContext(&_topTemplateContext, pC);
    std::vector<UnmangledItem> arguments;
    pC = UnmangleFunctionArguments(pC + 1, arguments);
    const char* indexBase = pC;
    size_t index = 1;
    if (*pC != '_') {
      index = 0;
      for (char c = *pC; c != '_'; c = *++pC) {
        if (c < '0' || c > '9') {
          throw indexBase;
        }
        index = (index * 10) + (*pC - '0');
      }
      index += 2;
    }
    pC++;

    uItem._unmangled = "{lambda(";
    if (!arguments.empty()) {
      size_t numArgs = arguments.size();
      uItem._unmangled.append(arguments[0]._unmangled);
      for (size_t argIndex = 1; argIndex < numArgs; ++argIndex) {
        uItem._unmangled.append(",");
        uItem._unmangled.append(arguments[argIndex]._unmangled);
      }
    }
    uItem._unmangled.append(")#");
    if (!AppendIndex(index, uItem)) {
      throw indexBase;
    }
    uItem._unmangled.append("}");

    _sValues.push_back(uItem);
    return pC;
  }

  static bool AppendIndex(size_t index, UnmangledItem& item) {
    size_t limit = 10;
    size_t numChars = 1;
    while (index > limit) {
      size_t oldLimit = limit;
      limit = limit * 10;
      if (limit < oldLimit) {
        return false;
      }
      numChars++;
    }
    char indexChars[25];
    for (size_t i = numChars - 1; i != 0; i--) {
      indexChars[i] = '0' + (index % 10);
      index /= 10;
    }
    indexChars[0] = '0' + index;

    item._unmangled.append(indexChars, numChars);
    return true;
  }

  const char* UnmangleParameterPack(const char* base, UnmangledItem& packItem) {
    bool needComma = false;
    const char* pC = base + 1;
    while (*pC != 'E') {
      if (needComma) {
        packItem._unmangled.append(",");
      } else {
        needComma = true;
      }
      UnmangledItem packMemberItem;
      if (*pC == 'J' || *pC == 'I') {
        pC = UnmangleParameterPack(pC, packMemberItem);
      } else {
        pC = UnmangleOneItem(pC, packMemberItem);
      }
      packItem._unmangled.append(packMemberItem._unmangled);
      packItem._packMembers.push_back(packMemberItem);
    }
    return pC + 1;
  }

  const char* AppendTemplateArguments(const char* base,
                                      UnmangledItem& nameItem) {
    if (base[0] != 'I') {
      throw base;
    }
    bool saveTemplateArguments =
        (_topTemplateContext != nullptr) && !_topTemplateContext->IsResolved();
    const char* pC = base + 1;
    nameItem._unmangled.append("<");
    bool needComma = false;
    while (*pC != 'E') {
      if (needComma) {
        nameItem._unmangled.append(",");
      } else {
        needComma = true;
      }
      UnmangledItem argumentItem;
      {
        TemplateContext templateContext(&_topTemplateContext, pC);
        if (*pC == 'J' || *pC == 'I') {
          pC = UnmangleParameterPack(pC, argumentItem);
        } else {
          pC = UnmangleOneItem(pC, argumentItem);
        }
      }
      if (saveTemplateArguments) {
        _topTemplateContext->AddItem(argumentItem);
      }
      nameItem._unmangled.append(argumentItem._unmangled);
    }
    nameItem._unmangled.append(">");
    if (saveTemplateArguments) {
      _topTemplateContext->Resolve();
    }
    return pC + 1;
  }

  const char* UnmangleSingleNameWithOptionalTemplateArguments(
      const char* base, UnmangledItem& nameItem) {
    const char* pC = UnmangleNameWithLength(base, nameItem);
    _sValues.push_back(nameItem);
    if (*pC == 'I') {
      pC = AppendTemplateArguments(pC, nameItem);
      _sValues.push_back(nameItem);
    }
    return pC;
  }

  const char* UnmangleNItem(const char* base, UnmangledItem& nItem) {
    bool saveTemplateArguments =
        (_topTemplateContext != nullptr) && !_topTemplateContext->IsResolved();
    const char* pC = base + 1;
    if (*pC == 'V') {
      nItem._hasVQualifier = true;
      pC++;
    }
    if (*pC == 'K') {
      nItem._hasKQualifier = true;
      pC++;
    }
    if (*pC == 'R') {
      nItem._hasRQualifier = true;
      pC++;
    }
    if (*pC == 'O') {
      nItem._hasOQualifier = true;
      pC++;
    }
    if (*pC == 'S') {
      pC = UnmangleSItem(pC, nItem);
    }
    if (*pC == 'T') {
      pC = UnmangleTItem(pC, nItem);
    }

    std::string lastName;
    for (char c = *pC; c != 'E'; c = *pC) {
      switch (c) {
        case 'B': {
          UnmangledItem bItem;
          pC = UnmangleBItem(pC, bItem);
          nItem._unmangled.append(bItem._unmangled);
        }
          _sValues.push_back(nItem);
          continue;
        case 'C':
          if (lastName.empty()) {
            throw pC;
          }
          nItem._unmangled.append("::");
          nItem._unmangled.append(lastName);
          _sValues.push_back(nItem);
          pC += 2;
          continue;
        case 'D':
          if (lastName.empty()) {
            throw pC;
          }
          nItem._unmangled.append("::~");
          nItem._unmangled.append(lastName);
          _sValues.push_back(nItem);
          pC += 2;
          continue;
        case 'I':
          if (nItem._unmangled.empty()) {
            throw base;
          }
          if (saveTemplateArguments) {
            _topTemplateContext->ClearTemplateArguments();
          }
          pC = AppendTemplateArguments(pC, nItem);
          _sValues.push_back(nItem);
          continue;
        case 'L':
          pC++;
          continue;
        case 'M':
          if (!nItem._unmangled.empty()) {
            nItem._unmangled.append("::");
          }
          {
            UnmangledItem mItem;
            pC = UnmangleMItem(pC, mItem);
            nItem._unmangled.append(mItem._unmangled);
          }
          continue;
        case 'U':
          if (!nItem._unmangled.empty()) {
            nItem._unmangled.append("::");
          }
          {
            UnmangledItem uItem;
            pC = UnmangleUItem(pC, uItem);
            nItem._unmangled.append(uItem._unmangled);
          }
          continue;
        case 'c':
          if (!nItem._unmangled.empty()) {
            nItem._unmangled.append("::");
          }
          nItem._unmangled.append("operator");
          c = *(++pC);
          if (c == 'l') {
            nItem._unmangled.append("()");
          } else if (c == 'm') {
            nItem._unmangled.append(",");
          } else if (c == 'o') {
            nItem._unmangled.append("~");
          } else {
            throw pC;
          }
          _sValues.push_back(nItem);
          if (saveTemplateArguments) {
            _topTemplateContext->ClearTemplateArguments();
          }
          ++pC;
          continue;
        default:
          if (c >= '0' && c <= '9') {
            if (!nItem._unmangled.empty()) {
              nItem._unmangled.append("::");
            }
            UnmangledItem nameItem;
            pC = UnmangleNameWithLength(pC, nameItem);
            nItem._unmangled.append(nameItem._unmangled);
            lastName = nameItem._unmangled;
            _sValues.push_back(nItem);
            if (saveTemplateArguments) {
              _topTemplateContext->ClearTemplateArguments();
            }
          } else {
            throw pC;
          }
      }
    }
    return pC + 1;
  }

  const char* UnmangleSItem(const char* base, UnmangledItem& sItem) {
    const char* pC = base + 1;
    char c = *pC;
    switch (c) {
      case 'a':
        sItem._unmangled = "std::allocator";
        if (*(++pC) == 'I') {
          pC = AppendTemplateArguments(pC, sItem);
          _sValues.push_back(sItem);
        }
        return pC;
      case 'b':
        sItem._unmangled.append("std::basic_string");
        return ++pC;
      case 'd':
        sItem._unmangled.append("std::iostream");
        return ++pC;
      case 'i':
        sItem._unmangled.append("std::istream");
        return ++pC;
      case 'o':
        sItem._unmangled.append("std::ostream");
        return ++pC;
      case 's':
        sItem._unmangled.append("std::string");
        return ++pC;
      case 't':
        sItem._unmangled.append("std::");
        return UnmangleSingleNameWithOptionalTemplateArguments(pC + 1, sItem);
      case '_':
        if (_sValues.empty()) {
          throw base;
        }
        sItem = _sValues[0];
        if (*(++pC) == 'I') {
          pC = AppendTemplateArguments(pC, sItem);
          _sValues.push_back(sItem);
        }
        return pC;
    }

    size_t index = 0;
    c = *pC;
    do {
      if (c >= '0' && c <= '9') {
        index = (index * 36) + (c - '0');
      } else if (c >= 'A' && c <= 'Z') {
        index = (index * 36) + 10 + (c - 'A');
      } else {
        throw base;
      }
      c = *(++pC);
    } while (c != '_');
    index += 1;
    if (index >= _sValues.size()) {
      throw pC;
    }
    sItem = _sValues[index];
    if (*(++pC) == 'I') {
      pC = AppendTemplateArguments(pC, sItem);
      _sValues.push_back(sItem);
    }
    return pC;
  }

  const char* UnmangleLItem(const char* base, UnmangledItem& lItem) {
    UnmangledItem startItem;
    const char* pC = UnmangleOneItem(base + 1, startItem);
    if (*pC == 'E') {
      lItem._unmangled = startItem._unmangled;
      return ++pC;
    }

    if (base[1] == 'b') {
      if (*pC == '0') {
        lItem._unmangled.append("false");
      } else if (*pC == '1') {
        lItem._unmangled.append("true");
      }
      if (*(++pC) != 'E') {
        throw base;
      }
      return ++pC;
    }

    const char* literalBase = pC;
    for (; *pC != 'E'; pC++) {
      if (*pC == '\000') {
        throw base;
      }
    }

    if (base[1] == 'i') {
      lItem._unmangled.append(literalBase, pC - literalBase);
      return pC + 1;
    }
    if (base[1] == 'j') {
      lItem._unmangled.append(literalBase, pC - literalBase);
      lItem._unmangled.append("u");
      return pC + 1;
    }
    if (base[1] == 'l') {
      lItem._unmangled.append(literalBase, pC - literalBase);
      lItem._unmangled.append("l");
      return pC + 1;
    }
    if (base[1] == 'm') {
      lItem._unmangled.append(literalBase, pC - literalBase);
      lItem._unmangled.append("ul");
      return pC + 1;
    }
    lItem._unmangled.append("(")
        .append(startItem._unmangled)
        .append(")")
        .append(literalBase, pC - literalBase);
    return pC + 1;
  }
  const char* UnmangleNameWithLength(const char* base, UnmangledItem& item) {
    int length = *base - '0';
    if (length < 0 || length > 9) {
      throw base;
    }
    const char* pC = base;
    for (char c = *(++pC); (c >= '0') && (c <= '9'); c = *(++pC)) {
      length = (length * 10) + (c - '0');
    }
    if (length > 1000 || (pC + length) > _mangledLimit) {
      throw base;
    }
    if (_checkAnonymousNamespace && std::string(pC, length) == "_GLOBAL__N_1") {
      item._unmangled.append("(anonymous)");
    } else {
      item._unmangled.append(pC, length);
    }
    return pC + length;
  }
};
}  // namespace CPlusPlus
}  // namespace chap
