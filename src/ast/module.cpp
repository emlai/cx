#include "module.h"
#include "ast-print.h"

using namespace cx;

llvm::StringMap<Module*> Module::allImportedModules;

std::vector<Module*> Module::getAllImportedModules() {
    return map(allImportedModules, [](auto& p) { return p.second; });
}

Module* Module::getStdlibModule() {
    auto it = allImportedModules.find("std");
    if (it == allImportedModules.end()) return nullptr;
    return it->second;
}

void Module::addToSymbolTableWithName(Decl& decl, llvm::StringRef name) {
    if (auto existing = getSymbolTable().findInCurrentScope(name); !existing.empty()) {
        REPORT_ERROR_WITH_NOTES(decl.getLocation(), getPreviousDefinitionNotes(existing), "redefinition of '" << name << "'");
    }

    if (decl.isGlobal()) {
        getSymbolTable().addGlobal(name, &decl);
    } else {
        getSymbolTable().add(name, &decl);
    }
}

void Module::addToSymbolTable(FunctionTemplate& decl) {
    if (auto existing = getSymbolTable().findWithMatchingPrototype(*decl.functionDecl)) {
        REPORT_ERROR_WITH_NOTES(decl.getLocation(), getPreviousDefinitionNotes(existing), "redefinition of '" << decl.getQualifiedName() << "'");
    }
    getSymbolTable().addGlobal(decl.getQualifiedName(), &decl);
}

void Module::addToSymbolTable(FunctionDecl& decl) {
    if (auto existing = getSymbolTable().findWithMatchingPrototype(decl)) {
        REPORT_ERROR_WITH_NOTES(decl.getLocation(), getPreviousDefinitionNotes(existing), "redefinition of '" << decl.getQualifiedName() << "'");
    }
    getSymbolTable().addGlobal(decl.getQualifiedName(), &decl);
}

void Module::addToSymbolTable(TypeTemplate& decl) {
    llvm::cast<BasicType>(decl.typeDecl->getType().getBase())->setDecl(decl.typeDecl);
    addToSymbolTableWithName(decl, decl.typeDecl->getName());
}

void Module::addToSymbolTable(TypeDecl& decl) {
    llvm::cast<BasicType>(decl.getType().getBase())->setDecl(&decl);
    addToSymbolTableWithName(decl, decl.getQualifiedName());

    for (auto& memberDecl : decl.methods) {
        if (auto* nonTemplateMethod = llvm::dyn_cast<MethodDecl>(memberDecl)) {
            addToSymbolTable(*nonTemplateMethod);
        }
    }
}

void Module::addToSymbolTable(EnumDecl& decl) {
    llvm::cast<BasicType>(decl.getType().getBase())->setDecl(&decl);
    addToSymbolTableWithName(decl, decl.getName());
}

void Module::addToSymbolTable(VarDecl& decl) {
    addToSymbolTableWithName(decl, decl.getName());
}

void Module::addToSymbolTable(Decl* decl) {
    getSymbolTable().add(decl->getName(), decl);

    if (auto* typeDecl = llvm::dyn_cast<TypeDecl>(decl)) {
        llvm::cast<BasicType>(*typeDecl->getType()).setDecl(typeDecl);
    }
}

void Module::addIdentifierReplacement(llvm::StringRef source, llvm::StringRef target) {
    ASSERT(!target.empty());
    getSymbolTable().addIdentifierReplacement(source, target);
}

void Module::print(llvm::raw_ostream& stream) const {
    for (auto& sourceFile : sourceFiles) {
        for (auto* topLevelDecl : sourceFile.getTopLevelDecls()) {
            stream << *topLevelDecl << "\n";
        }
    }
}

Scope::Scope(Decl* parent, SymbolTable* symbolTable) : parent(parent), symbolTable(symbolTable) {
    symbolTable->pushScope(*this);
}

Scope::~Scope() {
    symbolTable->popScope();
}
