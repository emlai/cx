#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Casting.h>
#include "expr.h"
#include "stmt.h"
#include "type.h"
#include "location.h"
#include "../support/utility.h"

namespace std {
template<>
struct hash<std::vector<delta::Type>> {
    size_t operator()(llvm::ArrayRef<delta::Type> types) const {
        ASSERT(!types.empty());
        size_t hashValue = reinterpret_cast<size_t>(types[0].get());

        for (auto type : types.drop_front()) {
            hashValue ^= reinterpret_cast<size_t>(type.get());
        }

        return hashValue;
    }
};
}

namespace llvm { class StringRef; }

namespace delta {

class Module;
class TypeDecl;
class FieldDecl;
class FunctionDecl;

enum class DeclKind {
    ParamDecl,
    GenericParamDecl,
    FunctionDecl,
    MethodDecl,
    InitDecl, /// A struct or class initializer declaration.
    DeinitDecl, /// A struct or class deinitializer declaration.
    FunctionTemplate,
    TypeDecl,
    TypeTemplate,
    VarDecl,
    FieldDecl, /// A struct or class field declaration.
    ImportDecl,
};

class Decl {
public:
    virtual ~Decl() = 0;

    bool isParamDecl() const { return getKind() == DeclKind::ParamDecl; }
    bool isFunctionDecl() const { return getKind() >= DeclKind::FunctionDecl && getKind() <= DeclKind::DeinitDecl; }
    bool isMethodDecl() const { return getKind() >= DeclKind::MethodDecl && getKind() <= DeclKind::DeinitDecl; }
    bool isGenericParamDecl() const { return getKind() == DeclKind::GenericParamDecl; }
    bool isInitDecl() const { return getKind() == DeclKind::InitDecl; }
    bool isDeinitDecl() const { return getKind() == DeclKind::DeinitDecl; }
    bool isFunctionTemplate() const { return getKind() == DeclKind::FunctionTemplate; }
    bool isTypeDecl() const { return getKind() == DeclKind::TypeDecl; }
    bool isTypeTemplate() const { return getKind() == DeclKind::TypeTemplate; }
    bool isVarDecl() const { return getKind() == DeclKind::VarDecl; }
    bool isFieldDecl() const { return getKind() == DeclKind::FieldDecl; }
    bool isImportDecl() const { return getKind() == DeclKind::ImportDecl; }

    DeclKind getKind() const { return kind; }
    Module* getModule() const;
    SourceLocation getLocation() const;
    bool hasBeenMoved() const;
    void markAsMoved();
    std::unique_ptr<Decl> instantiate(const llvm::StringMap<Type>& genericArgs,
                                      llvm::ArrayRef<Type> genericArgsArray) const;

protected:
    Decl(DeclKind kind) : kind(kind) {}

private:
    const DeclKind kind;
};

inline Decl::~Decl() {}

class Movable {
public:
    bool isMoved() const { return moved; }
    void setMoved(bool moved) { this->moved = moved; }

private:
    bool moved = false;
};

class ParamDecl : public Decl, public Movable {
public:
    ParamDecl(Type type, std::string&& name, SourceLocation location)
    : Decl(DeclKind::ParamDecl), type(type), name(std::move(name)), location(location),
      parent(nullptr) {}
    Type getType() const { return type; }
    llvm::StringRef getName() const { return name; }
    FunctionDecl* getParent() const { ASSERT(parent); return parent; }
    void setParent(FunctionDecl* parent) { this->parent = parent; }
    SourceLocation getLocation() const { return location; }
    static bool classof(const Decl* d) { return d->getKind() == DeclKind::ParamDecl; }
    bool operator==(const ParamDecl& other) const {
        return getType() == other.getType() && getName() == other.getName();
    }

private:
    Type type;
    std::string name;
    SourceLocation location;
    FunctionDecl* parent;
};

class GenericParamDecl : public Decl {
public:
    GenericParamDecl(std::string&& name, SourceLocation location)
    : Decl(DeclKind::GenericParamDecl), name(std::move(name)), parent(nullptr), location(location) {}
    llvm::StringRef getName() const { return name; }
    llvm::ArrayRef<std::string> getConstraints() const { return constraints; }
    void addConstraint(std::string&& constraint) { constraints.emplace_back(std::move(constraint)); }
    Decl* getParent() const { ASSERT(parent); return parent; }
    void setParent(Decl* parent) { this->parent = parent; }
    SourceLocation getLocation() const { return location; }
    static bool classof(const Decl* d) { return d->getKind() == DeclKind::GenericParamDecl; }

private:
    std::string name;
    llvm::SmallVector<std::string, 1> constraints;
    Decl* parent;
    SourceLocation location;
};

class FunctionProto {
public:
    FunctionProto(std::string&& name, std::vector<ParamDecl>&& params, Type returnType,
                  bool isVarArg, bool isExtern)
    : name(std::move(name)), params(std::move(params)), returnType(returnType),
      varArg(isVarArg), external(isExtern) {}
    llvm::StringRef getName() const { return name; }
    llvm::ArrayRef<ParamDecl> getParams() const { return params; }
    llvm::MutableArrayRef<ParamDecl> getParams() { return params; }
    Type getReturnType() const { return returnType; }
    bool isVarArg() const { return varArg; }
    bool isExtern() const { return external; }
    FunctionProto instantiate(const llvm::StringMap<Type> genericArgs) const;

private:
    std::string name;
    std::vector<ParamDecl> params;
    Type returnType;
    bool varArg;
    bool external;
};

class FunctionDecl : public Decl {
public:
    FunctionDecl(FunctionProto&& proto, std::vector<Type>&& genericArgs, Module& module,
                 SourceLocation location)
    : FunctionDecl(DeclKind::FunctionDecl, std::move(proto), std::move(genericArgs),
                   module, location) {}
    bool isExtern() const { return getProto().isExtern(); }
    bool isVariadic() const { return getProto().isVarArg(); }
    llvm::StringRef getName() const { return getProto().getName(); }
    llvm::ArrayRef<Type> getGenericArgs() const { return genericArgs; }
    Type getReturnType() const { return getProto().getReturnType(); }
    llvm::ArrayRef<ParamDecl> getParams() const { return getProto().getParams(); }
    llvm::MutableArrayRef<ParamDecl> getParams() { return getProto().getParams(); }
    const FunctionProto& getProto() const { return proto; }
    FunctionProto& getProto() { return proto; }
    virtual TypeDecl* getTypeDecl() const { return nullptr; }
    virtual bool isMutating() const { return false; }
    llvm::ArrayRef<std::unique_ptr<Stmt>> getBody() const { return body; }
    void setBody(std::vector<std::unique_ptr<Stmt>>&& body) { this->body = std::move(body); }
    SourceLocation getLocation() const { return location; }
    const FunctionType* getFunctionType() const;
    bool signatureMatches(const FunctionDecl& other, bool matchReceiver = true) const;
    Module* getModule() const { return &module; }
    std::unique_ptr<FunctionDecl> instantiate(const llvm::StringMap<Type>& genericArgs,
                                              llvm::ArrayRef<Type> genericArgsArray);
    static bool classof(const Decl* d) { return d->isFunctionDecl(); }

protected:
    FunctionDecl(DeclKind kind, FunctionProto&& proto, std::vector<Type>&& genericArgs,
                 Module& module, SourceLocation location)
    : Decl(kind), proto(std::move(proto)), genericArgs(std::move(genericArgs)),
      location(location), module(module) {}

private:
    FunctionProto proto;
    std::vector<Type> genericArgs;
    std::vector<std::unique_ptr<Stmt>> body;
    SourceLocation location;
    Module& module;
};

class MethodDecl : public FunctionDecl {
public:
    MethodDecl(FunctionProto proto, TypeDecl& receiverTypeDecl, SourceLocation location)
    : MethodDecl(DeclKind::MethodDecl, std::move(proto), receiverTypeDecl, location) {}
    bool isMutating() const override { return mutating; }
    void setMutating(bool mutating) { this->mutating = mutating; }
    TypeDecl* getTypeDecl() const override { return typeDecl; }
    Type getThisType() const;
    MethodDecl* instantiate(const llvm::StringMap<Type>& genericArgs, TypeDecl& typeDecl);
    static bool classof(const Decl* d) { return d->isMethodDecl(); }

protected:
    MethodDecl(DeclKind kind, FunctionProto proto, TypeDecl& typeDecl, SourceLocation location);

private:
    TypeDecl* typeDecl;
    bool mutating;
};

class InitDecl : public MethodDecl {
public:
    InitDecl(TypeDecl& receiverTypeDecl, std::vector<ParamDecl>&& params,
             std::vector<std::unique_ptr<Stmt>>&& body, SourceLocation location)
    : MethodDecl(DeclKind::InitDecl,
                 FunctionProto("init", std::move(params), Type::getVoid(), false, false),
                 receiverTypeDecl, location) {
        setBody(std::move(body));
    }
    bool isMutating() const override { return true; }
    static bool classof(const Decl* d) { return d->getKind() == DeclKind::InitDecl; }
};

class DeinitDecl : public MethodDecl {
public:
    DeinitDecl(TypeDecl& receiverTypeDecl, std::vector<std::unique_ptr<Stmt>>&& body,
               SourceLocation location)
    : MethodDecl(DeclKind::DeinitDecl,
                 FunctionProto("deinit", {}, Type::getVoid(), false, false),
                 receiverTypeDecl, location) {
        setBody(std::move(body));
    }
    static bool classof(const Decl* d) { return d->getKind() == DeclKind::DeinitDecl; }
};

class FunctionTemplate : public Decl {
public:
    FunctionTemplate(std::vector<GenericParamDecl>&& genericParams,
                     std::unique_ptr<FunctionDecl> functionDecl)
    : Decl(DeclKind::FunctionTemplate), genericParams(std::move(genericParams)),
      functionDecl(std::move(functionDecl)) {}
    bool signatureMatches(const FunctionDecl& other, bool matchReceiver = true) const;
    static bool classof(const Decl* d) { return d->isFunctionTemplate(); }
    llvm::ArrayRef<GenericParamDecl> getGenericParams() const { return genericParams; }
    FunctionDecl* getFunctionDecl() const { return functionDecl.get(); }
    FunctionDecl* instantiate(const llvm::StringMap<Type>& genericArgs);
    FunctionDecl* instantiate(llvm::ArrayRef<Type> genericArgs);

private:
    std::vector<GenericParamDecl> genericParams;
    std::unique_ptr<FunctionDecl> functionDecl;
    std::unordered_map<std::vector<Type>, std::unique_ptr<FunctionDecl>> instantiations;
};

enum class TypeTag { Struct, Class, Interface, Union };

/// A non-template function declaration or a function template instantiation.
class TypeDecl : public Decl {
public:
    TypeDecl(TypeTag tag, std::string&& name, std::vector<Type>&& genericArgs, Module& module,
             SourceLocation location)
    : Decl(DeclKind::TypeDecl), tag(tag), name(std::move(name)),
      genericArgs(std::move(genericArgs)), location(location), module(module) {}
    TypeTag getTag() const { return tag; }
    llvm::StringRef getName() const { return name; }
    llvm::ArrayRef<FieldDecl> getFields() const { return fields; }
    std::vector<FieldDecl>& getFields() { return fields; }
    llvm::ArrayRef<std::unique_ptr<MethodDecl>> getMethods() const { return methods; }
    llvm::ArrayRef<Type> getGenericArgs() const { return genericArgs; }
    SourceLocation getLocation() const { return location; }
    void addField(FieldDecl&& field);
    void addMethod(std::unique_ptr<MethodDecl> decl);
    llvm::ArrayRef<std::unique_ptr<MethodDecl>> getMemberDecls() const { return methods; }
    DeinitDecl* getDeinitializer() const;
    Type getType(bool isMutable = false) const;
    /// 'T&' if this is class, or plain 'T' otherwise.
    Type getTypeForPassing(bool isMutable = false) const;
    bool passByValue() const { return isStruct() || isUnion(); }
    bool isStruct() const { return tag == TypeTag::Struct; }
    bool isClass() const { return tag == TypeTag::Class; }
    bool isInterface() const { return tag == TypeTag::Interface; }
    bool isUnion() const { return tag == TypeTag::Union; }
    unsigned getFieldIndex(llvm::StringRef fieldName) const;
    Module* getModule() const { return &module; }
    static bool classof(const Decl* d) { return d->getKind() == DeclKind::TypeDecl; }

private:
    TypeTag tag;
    std::string name;
    std::vector<Type> genericArgs;
    std::vector<FieldDecl> fields;
    std::vector<std::unique_ptr<MethodDecl>> methods;
    SourceLocation location;
    Module& module;
};

class TypeTemplate : public Decl {
public:
    TypeTemplate(std::vector<GenericParamDecl>&& genericParams, std::unique_ptr<TypeDecl> typeDecl)
    : Decl(DeclKind::TypeTemplate), genericParams(std::move(genericParams)), typeDecl(std::move(typeDecl)) {}
    llvm::ArrayRef<GenericParamDecl> getGenericParams() const { return genericParams; }
    TypeDecl* getTypeDecl() const { return typeDecl.get(); }
    TypeDecl* instantiate(const llvm::StringMap<Type>& genericArgs);
    TypeDecl* instantiate(llvm::ArrayRef<Type> genericArgs);
    static bool classof(const Decl* d) { return d->getKind() == DeclKind::TypeTemplate; }

private:
    std::vector<GenericParamDecl> genericParams;
    std::unique_ptr<TypeDecl> typeDecl;
    std::unordered_map<std::vector<Type>, std::unique_ptr<TypeDecl>> instantiations;
};

class VarDecl : public Decl, public Movable {
public:
    VarDecl(Type type, std::string&& name, std::shared_ptr<Expr>&& initializer,
            Module& module, SourceLocation location)
    : Decl(DeclKind::VarDecl), type(type), name(std::move(name)),
      initializer(std::move(initializer)), location(location), module(module) {}
    Type getType() const { return type; }
    void setType(Type type) { this->type = type; }
    llvm::StringRef getName() const { return name; }
    Expr* getInitializer() const { return initializer.get(); }
    SourceLocation getLocation() const { return location; }
    Module* getModule() const { return &module; }
    static bool classof(const Decl* d) { return d->getKind() == DeclKind::VarDecl; }

private:
    Type type;
    std::string name;
    std::shared_ptr<Expr> initializer; /// Null if the initializer is 'undefined'.
    SourceLocation location;
    Module& module;
};

class FieldDecl : public Decl {
public:
    FieldDecl(Type type, std::string&& name, TypeDecl& parent, SourceLocation location)
    : Decl(DeclKind::FieldDecl), type(type), name(std::move(name)), location(location),
      parent(parent) {}
    Type getType() const { return type; }
    llvm::StringRef getName() const { return name; }
    SourceLocation getLocation() const { return location; }
    TypeDecl* getParent() const { return &parent; }
    static bool classof(const Decl* d) { return d->getKind() == DeclKind::FieldDecl; }

private:
    Type type;
    std::string name;
    SourceLocation location;
    TypeDecl& parent;
};

class ImportDecl : public Decl {
public:
    ImportDecl(std::string&& target, Module& module, SourceLocation location)
    : Decl(DeclKind::ImportDecl), target(std::move(target)), location(location), module(module) {}
    llvm::StringRef getTarget() const { return target; }
    SourceLocation getLocation() const { return location; }
    Module* getModule() const { return &module; }
    static bool classof(const Decl* d) { return d->getKind() == DeclKind::ImportDecl; }

private:
    std::string target;
    SourceLocation location;
    Module& module;
};

}
