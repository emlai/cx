#include "type.h"
#include <sstream>
#pragma warning(push, 0)
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSwitch.h>
#include <llvm/Support/ErrorHandling.h>
#pragma warning(pop)
#include "decl.h"
#include "../support/utility.h"

using namespace delta;

static std::vector<std::unique_ptr<TypeBase>> typeBases;

#define DEFINE_BUILTIN_TYPE_GET_AND_IS(TYPE, NAME) \
    Type Type::get##TYPE(Mutability mutability, SourceLocation location) { \
        static BasicType type(#NAME, /*genericArgs*/ {}); \
        return Type(&type, mutability, location); \
    } \
    bool Type::is##TYPE() const { return isBasicType() && getName() == #NAME; }

DEFINE_BUILTIN_TYPE_GET_AND_IS(Void, void)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Bool, bool)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Int, int)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Int8, int8)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Int16, int16)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Int32, int32)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Int64, int64)
DEFINE_BUILTIN_TYPE_GET_AND_IS(UInt, uint)
DEFINE_BUILTIN_TYPE_GET_AND_IS(UInt8, uint8)
DEFINE_BUILTIN_TYPE_GET_AND_IS(UInt16, uint16)
DEFINE_BUILTIN_TYPE_GET_AND_IS(UInt32, uint32)
DEFINE_BUILTIN_TYPE_GET_AND_IS(UInt64, uint64)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Float, float)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Float32, float32)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Float64, float64)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Float80, float80)
DEFINE_BUILTIN_TYPE_GET_AND_IS(String, String)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Char, char)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Null, null)
DEFINE_BUILTIN_TYPE_GET_AND_IS(Undefined, undefined)
#undef DEFINE_BUILTIN_TYPE_GET_AND_IS

bool Type::isImplicitlyCopyable() const {
    switch (getKind()) {
        case TypeKind::BasicType:
            return !getDecl() || getDecl()->passByValue();
        case TypeKind::ArrayType:
            return false;
        case TypeKind::TupleType:
            return llvm::all_of(llvm::cast<TupleType>(typeBase)->getElements(),
                                [&](auto& element) { return element.type.isImplicitlyCopyable(); });
        case TypeKind::FunctionType:
            return true;
        case TypeKind::PointerType:
            return true;
        case TypeKind::OptionalType:
            return getWrappedType().isImplicitlyCopyable();
    }
    llvm_unreachable("all cases handled");
}

bool Type::isArrayWithConstantSize() const {
    return isArrayType() && getArraySize() >= 0;
}

bool Type::isArrayWithRuntimeSize() const {
    return isArrayType() && getArraySize() == ArrayType::runtimeSize;
}

bool Type::isArrayWithUnknownSize() const {
    return isArrayType() && getArraySize() == ArrayType::unknownSize;
}

bool Type::isBuiltinScalar(llvm::StringRef typeName) {
    return llvm::StringSwitch<bool>(typeName)
        .Cases("int", "int8", "int16", "int32", "int64", true)
        .Cases("uint", "uint8", "uint16", "uint32", "uint64", true)
        .Cases("float", "float32", "float64", "float80", "bool", "char", true)
        .Default(false);
}

bool Type::isEnumType() const {
    if (auto* basicType = llvm::dyn_cast<BasicType>(getBase())) {
        return basicType->getDecl() && basicType->getDecl()->isEnumDecl();
    }
    return false;
}

Type Type::resolve(const llvm::StringMap<Type>& replacements) const {
    if (!typeBase) return Type(nullptr, mutability, location);

    switch (getKind()) {
        case TypeKind::BasicType: {
            auto it = replacements.find(getName());
            if (it != replacements.end()) {
                // TODO: Handle generic arguments for type placeholders.
                Type resolved = it->second.withMutability(mutability);
                resolved.setLocation(location);
                return resolved;
            }

            auto genericArgs = map(getGenericArgs(), [&](Type t) { return t.resolve(replacements); });
            return BasicType::get(getName(), std::move(genericArgs), mutability, location);
        }
        case TypeKind::ArrayType:
            return ArrayType::get(getElementType().resolve(replacements), getArraySize(), mutability, location);

        case TypeKind::TupleType: {
            auto elements = map(getTupleElements(), [&](const TupleElement& element) {
                return TupleElement{ element.name, element.type.resolve(replacements) };
            });
            return TupleType::get(std::move(elements), mutability, location);
        }
        case TypeKind::FunctionType: {
            auto paramTypes = map(getParamTypes(), [&](Type t) { return t.resolve(replacements); });
            return FunctionType::get(getReturnType().resolve(replacements), std::move(paramTypes), mutability, location);
        }
        case TypeKind::PointerType:
            return PointerType::get(getPointee().resolve(replacements), mutability, location);
        case TypeKind::OptionalType:
            return OptionalType::get(getWrappedType().resolve(replacements), mutability, location);
    }
    llvm_unreachable("all cases handled");
}

template<typename T>
static Type getType(T&& typeBase, Mutability mutability, SourceLocation location) {
    Type newType(&typeBase, mutability, location);

    for (auto& existingTypeBase : typeBases) {
        Type existingType(&*existingTypeBase, mutability, location);
        if (existingType.equalsIgnoreTopLevelMutable(newType)) {
            return existingType;
        }
    }

    typeBases.push_back(llvm::make_unique<T>(std::forward<T>(typeBase)));
    return Type(&*typeBases.back(), mutability, location);
}

Type BasicType::get(llvm::StringRef name, llvm::ArrayRef<Type> genericArgs, Mutability mutability, SourceLocation location) {
    return getType(BasicType(name, genericArgs), mutability, location);
}

Type ArrayType::get(Type elementType, int64_t size, Mutability mutability, SourceLocation location) {
    return getType(ArrayType(elementType, size), mutability, location);
}

Type TupleType::get(std::vector<TupleElement>&& elements, Mutability mutability, SourceLocation location) {
    return getType(TupleType(std::move(elements)), mutability, location);
}

Type FunctionType::get(Type returnType, std::vector<Type>&& paramTypes, Mutability mutability, SourceLocation location) {
    return getType(FunctionType(returnType, std::move(paramTypes)), mutability, location);
}

Type PointerType::get(Type pointeeType, Mutability mutability, SourceLocation location) {
    return getType(PointerType(pointeeType), mutability, location);
}

Type OptionalType::get(Type wrappedType, Mutability mutability, SourceLocation location) {
    return getType(OptionalType(wrappedType), mutability, location);
}

bool delta::operator==(const TupleElement& a, const TupleElement& b) {
    return a.name == b.name && a.type == b.type;
}

void delta::appendGenericArgs(std::string& typeName, llvm::ArrayRef<Type> genericArgs) {
    if (genericArgs.empty()) return;

    typeName += '<';
    for (const Type& genericArg : genericArgs) {
        typeName += genericArg.toString(false);
        if (&genericArg != &genericArgs.back()) typeName += ", ";
    }
    typeName += '>';
}

std::string delta::getQualifiedTypeName(llvm::StringRef typeName, llvm::ArrayRef<Type> genericArgs) {
    std::string result = typeName;
    appendGenericArgs(result, genericArgs);
    return result;
}

std::vector<ParamDecl> FunctionType::getParamDecls(SourceLocation location) const {
    std::vector<ParamDecl> paramDecls;
    paramDecls.reserve(paramTypes.size());

    for (Type paramType : paramTypes) {
        paramDecls.push_back(ParamDecl(paramType, "", location));
    }

    return paramDecls;
}

constexpr auto signedInts = { "int", "int8", "int16", "int32", "int64" };
constexpr auto unsignedInts = { "uint", "uint8", "uint16", "uint32", "uint64" };

bool Type::isInteger() const {
    if (!isBasicType()) return false;
    return llvm::is_contained(signedInts, getName()) || llvm::is_contained(unsignedInts, getName());
}

bool Type::isSigned() const {
    ASSERT(isInteger());
    return llvm::is_contained(signedInts, getName());
}

bool Type::isUnsigned() const {
    ASSERT(isInteger());
    return llvm::is_contained(unsignedInts, getName());
}

llvm::StringRef Type::getName() const {
    return llvm::cast<BasicType>(typeBase)->getName();
}

std::string Type::getQualifiedTypeName() const {
    return llvm::cast<BasicType>(typeBase)->getQualifiedName();
}

Type Type::getElementType() const {
    return llvm::cast<ArrayType>(typeBase)->getElementType().withLocation(location);
}

int64_t Type::getArraySize() const {
    return llvm::cast<ArrayType>(typeBase)->getSize();
}

llvm::ArrayRef<TupleElement> Type::getTupleElements() const {
    return llvm::cast<TupleType>(typeBase)->getElements();
}

llvm::ArrayRef<Type> Type::getGenericArgs() const {
    return llvm::cast<BasicType>(typeBase)->getGenericArgs();
}

Type Type::getReturnType() const {
    return llvm::cast<FunctionType>(typeBase)->getReturnType().withLocation(location);
}

llvm::ArrayRef<Type> Type::getParamTypes() const {
    return llvm::cast<FunctionType>(typeBase)->getParamTypes();
}

Type Type::getPointee() const {
    return llvm::cast<PointerType>(typeBase)->getPointeeType().withLocation(location);
}

Type Type::getWrappedType() const {
    return llvm::cast<OptionalType>(typeBase)->getWrappedType().withLocation(location);
}

bool delta::operator==(Type lhs, Type rhs) {
    if (lhs.isMutable() != rhs.isMutable()) return false;
    return lhs.equalsIgnoreTopLevelMutable(rhs);
}

bool Type::equalsIgnoreTopLevelMutable(Type other) const {
    switch (getKind()) {
        case TypeKind::BasicType:
            return other.isBasicType() && getName() == other.getName() && getGenericArgs() == other.getGenericArgs();
        case TypeKind::ArrayType:
            return other.isArrayType() && getElementType() == other.getElementType() && getArraySize() == other.getArraySize();
        case TypeKind::TupleType:
            return other.isTupleType() && getTupleElements() == other.getTupleElements();
        case TypeKind::FunctionType:
            return other.isFunctionType() && getReturnType() == other.getReturnType() && getParamTypes() == other.getParamTypes();
        case TypeKind::PointerType:
            return other.isPointerType() && getPointee() == other.getPointee();
        case TypeKind::OptionalType:
            return other.isOptionalType() && getWrappedType() == other.getWrappedType();
    }
    llvm_unreachable("all cases handled");
}

bool delta::operator!=(Type lhs, Type rhs) {
    return !(lhs == rhs);
}

TypeDecl* Type::getDecl() const {
    auto* basicType = llvm::dyn_cast<BasicType>(typeBase);
    return basicType ? basicType->getDecl() : nullptr;
}

DeinitDecl* Type::getDeinitializer() const {
    auto* typeDecl = getDecl();
    return typeDecl ? typeDecl->getDeinitializer() : nullptr;
}

void Type::printTo(std::ostream& stream, bool omitTopLevelConst) const {
    switch (typeBase->getKind()) {
        case TypeKind::BasicType: {
            if (!isMutable() && !omitTopLevelConst) stream << "const ";
            stream << getName();

            auto genericArgs = llvm::cast<BasicType>(typeBase)->getGenericArgs();
            if (!genericArgs.empty()) {
                stream << "<";
                for (auto& type : genericArgs) {
                    type.printTo(stream, false);
                    if (&type != &genericArgs.back()) stream << ", ";
                }
                stream << ">";
            }

            break;
        }
        case TypeKind::ArrayType:
            getElementType().printTo(stream, omitTopLevelConst);
            stream << "[";
            switch (getArraySize()) {
                case ArrayType::runtimeSize:
                    break;
                case ArrayType::unknownSize:
                    stream << "?";
                    break;
                default:
                    stream << getArraySize();
                    break;
            }
            stream << "]";
            break;
        case TypeKind::TupleType:
            stream << "(";
            for (auto& element : getTupleElements()) {
                element.type.printTo(stream, omitTopLevelConst);
                stream << " " << element.name;
                if (&element != &getTupleElements().back()) stream << ", ";
            }
            stream << ")";
            break;
        case TypeKind::FunctionType:
            getReturnType().printTo(stream, true);
            stream << "(";
            for (const Type& paramType : getParamTypes()) {
                stream << paramType;
                if (&paramType != &getParamTypes().back()) stream << ", ";
            }
            stream << ")";
            break;
        case TypeKind::PointerType:
            if (getPointee().isFunctionType()) {
                stream << '(';
            }
            getPointee().printTo(stream, false);
            if (!isMutable() && !omitTopLevelConst) {
                stream << " const";
            }
            if (getPointee().isFunctionType()) {
                stream << ')';
            }
            stream << '*';
            break;
        case TypeKind::OptionalType:
            if (getWrappedType().isFunctionType()) {
                stream << '(';
            }
            getWrappedType().printTo(stream, false);
            if (!isMutable() && !omitTopLevelConst) {
                stream << " const";
            }
            if (getWrappedType().isFunctionType()) {
                stream << ')';
            }
            stream << '?';
            break;
    }
}

std::string Type::toString(bool omitTopLevelMutable) const {
    std::ostringstream stream;
    printTo(stream, omitTopLevelMutable);
    return stream.str();
}

std::ostream& delta::operator<<(std::ostream& stream, Type type) {
    type.printTo(stream, true);
    return stream;
}

llvm::raw_ostream& delta::operator<<(llvm::raw_ostream& stream, Type type) {
    std::ostringstream stringstream;
    type.printTo(stringstream, true);
    return stream << stringstream.str();
}
