#include "stmt.h"
#include "ast.h"
#include "decl.h"

using namespace cx;

bool Stmt::isBreakable() const {
    switch (kind) {
    case StmtKind::WhileStmt:
    case StmtKind::ForStmt:
    case StmtKind::ForEachStmt:
    case StmtKind::SwitchStmt:
        return true;
    default:
        return false;
    }
}

bool Stmt::isContinuable() const {
    switch (kind) {
    case StmtKind::WhileStmt:
    case StmtKind::ForStmt:
    case StmtKind::ForEachStmt:
        return true;
    default:
        return false;
    }
}

Stmt* Stmt::instantiate(const llvm::StringMap<Type>& genericArgs) const {
    switch (kind) {
    case StmtKind::ReturnStmt: {
        auto* returnStmt = llvm::cast<ReturnStmt>(this);
        auto returnValue = returnStmt->value ? returnStmt->value->instantiate(genericArgs) : nullptr;
        return new ReturnStmt(returnValue, returnStmt->location);
    }
    case StmtKind::VarStmt: {
        auto* varStmt = llvm::cast<VarStmt>(this);
        auto instantiation = varStmt->decl->instantiate(genericArgs, {});
        return new VarStmt(llvm::cast<VarDecl>(instantiation));
    }
    case StmtKind::ExprStmt: {
        auto* exprStmt = llvm::cast<ExprStmt>(this);
        return new ExprStmt(exprStmt->expr->instantiate(genericArgs));
    }
    case StmtKind::DeferStmt: {
        auto* deferStmt = llvm::cast<DeferStmt>(this);
        return new DeferStmt(deferStmt->expr->instantiate(genericArgs));
    }
    case StmtKind::IfStmt: {
        auto* ifStmt = llvm::cast<IfStmt>(this);
        auto condition = ifStmt->condition->instantiate(genericArgs);
        auto thenBody = ::instantiate(ifStmt->thenBody, genericArgs);
        auto elseBody = ::instantiate(ifStmt->elseBody, genericArgs);
        return new IfStmt(condition, std::move(thenBody), std::move(elseBody));
    }
    case StmtKind::SwitchStmt: {
        auto* switchStmt = llvm::cast<SwitchStmt>(this);
        auto condition = switchStmt->condition->instantiate(genericArgs);
        auto cases = map(switchStmt->cases, [&](const SwitchCase& switchCase) {
            auto value = switchCase.value->instantiate(genericArgs);
            auto associatedValue = llvm::cast<VarDecl>(switchCase.associatedValue->instantiate(genericArgs, {}));
            auto stmts = ::instantiate(switchCase.stmts, genericArgs);
            return SwitchCase(value, associatedValue, std::move(stmts));
        });
        auto defaultStmts = ::instantiate(switchStmt->defaultStmts, genericArgs);
        return new SwitchStmt(condition, std::move(cases), std::move(defaultStmts));
    }
    case StmtKind::WhileStmt: {
        auto* whileStmt = llvm::cast<WhileStmt>(this);
        auto condition = whileStmt->condition->instantiate(genericArgs);
        auto body = ::instantiate(whileStmt->body, genericArgs);
        return new WhileStmt(condition, std::move(body), whileStmt->location);
    }
    case StmtKind::ForStmt: {
        auto* forStmt = llvm::cast<ForStmt>(this);
        auto variable = forStmt->variable ? llvm::cast<VarStmt>(forStmt->variable->instantiate(genericArgs)) : nullptr;
        auto condition = forStmt->condition ? forStmt->condition->instantiate(genericArgs) : nullptr;
        auto increment = forStmt->increment ? forStmt->increment->instantiate(genericArgs) : nullptr;
        auto body = ::instantiate(forStmt->body, genericArgs);
        return new ForStmt(variable, condition, increment, std::move(body), forStmt->location);
    }
    case StmtKind::ForEachStmt: {
        auto* forEachStmt = llvm::cast<ForEachStmt>(this);
        // The second argument can be empty because VarDecl instantiation doesn't use it.
        auto variable = llvm::cast<VarDecl>(forEachStmt->variable->instantiate(genericArgs, {}));
        auto range = forEachStmt->range->instantiate(genericArgs);
        auto body = ::instantiate(forEachStmt->body, genericArgs);
        return new ForEachStmt(variable, range, std::move(body), forEachStmt->location);
    }
    case StmtKind::BreakStmt: {
        auto* breakStmt = llvm::cast<BreakStmt>(this);
        return new BreakStmt(breakStmt->location);
    }
    case StmtKind::ContinueStmt: {
        auto* continueStmt = llvm::cast<ContinueStmt>(this);
        return new ContinueStmt(continueStmt->location);
    }
    case StmtKind::CompoundStmt: {
        auto* compoundStmt = llvm::cast<CompoundStmt>(this);
        auto body = ::instantiate(compoundStmt->body, genericArgs);
        return new CompoundStmt(std::move(body));
    }
    }
    llvm_unreachable("all cases handled");
}

Stmt* WhileStmt::lower() {
    return new ForStmt(nullptr, condition, nullptr, std::move(body), location);
}

// Lowers 'for (var id in range) { ... }' into:
// for (var __iterator = range.iterator(); __iterator.hasValue(); __iterator.increment()) {
//     var id = __iterator.value();
//     ...
// }
Stmt* ForEachStmt::lower(int nestLevel) {
    auto iteratorVariableName = "__iterator" + (nestLevel > 0 ? std::to_string(nestLevel) : "");

    Expr* iteratorValue;
    auto* rangeTypeDecl = range->getType().removePointer().getDecl();
    bool isIterator = rangeTypeDecl && llvm::any_of(rangeTypeDecl->interfaces, [](Type interface) { return interface.getName() == "Iterator"; });

    if (isIterator) {
        iteratorValue = range;
    } else {
        auto iteratorMemberExpr = new MemberExpr(range, "iterator", location);
        iteratorValue = new CallExpr(iteratorMemberExpr, std::vector<NamedValue>(), std::vector<Type>(), location);
    }

    auto iteratorVarDecl = new VarDecl(Type(nullptr, Mutability::Mutable, location), std::string(iteratorVariableName), iteratorValue, variable->parent,
                                       AccessLevel::None, *variable->getModule(), location);
    auto iteratorVarStmt = new VarStmt(iteratorVarDecl);

    auto iteratorVarExpr = new VarExpr(std::string(iteratorVariableName), location);
    auto hasValueMemberExpr = new MemberExpr(iteratorVarExpr, "hasValue", location);
    auto hasValueCallExpr = new CallExpr(hasValueMemberExpr, std::vector<NamedValue>(), std::vector<Type>(), location);

    auto iteratorVarExpr2 = new VarExpr(std::string(iteratorVariableName), location);
    auto valueMemberExpr = new MemberExpr(iteratorVarExpr2, "value", location);
    auto valueCallExpr = new CallExpr(valueMemberExpr, std::vector<NamedValue>(), std::vector<Type>(), location);
    auto loopVariableVarDecl = new VarDecl(variable->type, variable->getName().str(), valueCallExpr, variable->parent, AccessLevel::None,
                                           *variable->getModule(), variable->getLocation());
    auto loopVariableVarStmt = new VarStmt(loopVariableVarDecl);

    std::vector<Stmt*> forBody;
    forBody.push_back(loopVariableVarStmt);

    for (auto& stmt : body) {
        forBody.push_back(stmt);
    }

    auto iteratorVarExpr3 = new VarExpr(std::string(iteratorVariableName), location);
    auto incrementMemberExpr = new MemberExpr(iteratorVarExpr3, "increment", location);
    auto incrementCallExpr = new CallExpr(incrementMemberExpr, std::vector<NamedValue>(), std::vector<Type>(), location);
    return new ForStmt(iteratorVarStmt, hasValueCallExpr, incrementCallExpr, std::move(forBody), location);
}
