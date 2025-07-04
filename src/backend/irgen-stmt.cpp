#include "irgen.h"

using namespace cx;

void IRGenerator::emitReturnStmt(const ReturnStmt& stmt) {
    // TODO: Emit deferred expressions and destructor calls after the evaluation of the return
    // value, but before emitting the return instruction. The return value expression may depend on
    // the values that the deferred expressions and/or destructor calls could deallocate.
    emitDeferredExprsAndDestructorCallsForReturn();

    if (auto* returnValue = stmt.value) {
        createReturn(emitExprForPassing(*returnValue, insertBlock->parent->returnType));
    } else {
        createReturn(currentDecl->isMain() ? createConstantInt(Type::getInt(), 0) : nullptr);
    }
}

void IRGenerator::emitBlock(llvm::ArrayRef<Stmt*> stmts, BasicBlock* continuation) {
    beginScope();
    emitStmts(stmts);
    endScope();

    if (insertBlock->body.empty() || !insertBlock->body.back()->isTerminator()) {
        createBr(continuation);
    }
}

void IRGenerator::emitIfStmt(const IfStmt& ifStmt) {
    auto* condition = emitExpr(*ifStmt.condition);

    // FIXME: Lower implicit null checks such as `if (ptr)` and `if (!ptr)` to null comparisons.
    if (condition->getType()->isPointerType()) {
        condition = emitImplicitNullComparison(condition);
    } else if (ifStmt.condition->getType().isOptionalType() && !ifStmt.condition->getType().getWrappedType().isPointerType()) {
        condition = createExtractValue(condition, optionalHasValueFieldIndex);
    }

    auto* function = insertBlock->parent;
    auto* thenBlock = new BasicBlock("if.then", function);
    auto* elseBlock = new BasicBlock("if.else", function);
    auto* endIfBlock = new BasicBlock("if.end", function);
    createCondBr(condition, thenBlock, elseBlock);

    setInsertPoint(thenBlock);
    emitBlock(ifStmt.thenBody, endIfBlock);

    setInsertPoint(elseBlock);
    emitBlock(ifStmt.elseBody, endIfBlock);

    setInsertPoint(endIfBlock);
}

void IRGenerator::emitSwitchStmt(const SwitchStmt& switchStmt) {
    Value* enumValue = nullptr;
    Value* condition = emitExprOrEnumTag(*switchStmt.condition, &enumValue);

    auto* function = insertBlock->parent;
    auto* insertBlockBackup = insertBlock;
    auto caseIndex = 0;

    auto cases = map(switchStmt.cases, [&](const SwitchCase& switchCase) {
        auto* value = emitExprOrEnumTag(*switchCase.value, nullptr);
        auto* block = new BasicBlock("switch.case." + std::to_string(caseIndex++), function);
        return std::make_pair(value, block);
    });

    setInsertPoint(insertBlockBackup);
    auto* defaultBlock = new BasicBlock("switch.default", function);
    auto* end = new BasicBlock("switch.end", function);
    breakTargets.push_back(end);
    auto* switchInst = createSwitch(condition, defaultBlock);

    auto casesIterator = cases.begin();
    for (auto& switchCase : switchStmt.cases) {
        auto* value = casesIterator->first;
        auto* block = casesIterator->second;
        setInsertPoint(block);

        if (auto* associatedValue = switchCase.associatedValue) {
            auto type = associatedValue->type.getPointerTo();
            auto* associatedValuePtr = createCast(createGEP(enumValue, 1), type, associatedValue->getName());
            setLocalValue(associatedValuePtr, associatedValue);
        }

        emitBlock(switchCase.stmts, end);
        switchInst->cases.emplace_back(value, block);
        ++casesIterator;
    }

    setInsertPoint(defaultBlock);
    emitBlock(switchStmt.defaultStmts, end);

    breakTargets.pop_back();
    setInsertPoint(end);
}

void IRGenerator::emitForStmt(const ForStmt& forStmt) {
    if (forStmt.variable) {
        emitVarDecl(*forStmt.variable->decl);
    }

    auto* increment = forStmt.increment;
    auto* function = insertBlock->parent;
    auto* condition = new BasicBlock("loop.condition", function);
    auto* body = new BasicBlock("loop.body", function);
    auto* afterBody = increment ? new BasicBlock("loop.increment", function) : condition;
    auto* end = new BasicBlock("loop.end", function);

    breakTargets.push_back(end);
    continueTargets.push_back(afterBody);
    createBr(condition);

    setInsertPoint(condition);
    auto* conditionValue = emitExpr(*forStmt.condition);
    if (conditionValue->getType()->isPointerType()) {
        conditionValue = emitImplicitNullComparison(conditionValue);
    }
    createCondBr(conditionValue, body, end);

    setInsertPoint(body);
    emitBlock(forStmt.body, afterBody);

    if (increment) {
        setInsertPoint(afterBody);
        emitExpr(*increment);
        createBr(condition);
    }

    breakTargets.pop_back();
    continueTargets.pop_back();
    setInsertPoint(end);
}

void IRGenerator::emitBreakStmt(const BreakStmt&) {
    ASSERT(!breakTargets.empty());
    createBr(breakTargets.back());
}

void IRGenerator::emitContinueStmt(const ContinueStmt&) {
    ASSERT(!continueTargets.empty());
    createBr(continueTargets.back());
}

void IRGenerator::emitCompoundStmt(const CompoundStmt& compoundStmt) {
    beginScope();
    emitStmts(compoundStmt.body);
    endScope();
}

void IRGenerator::emitStmt(const Stmt& stmt) {
    switch (stmt.kind) {
    case StmtKind::ReturnStmt:
        emitReturnStmt(llvm::cast<ReturnStmt>(stmt));
        break;
    case StmtKind::VarStmt:
        emitVarDecl(*llvm::cast<VarStmt>(stmt).decl);
        break;
    case StmtKind::ExprStmt:
        emitPlainExpr(*llvm::cast<ExprStmt>(stmt).expr);
        break;
    case StmtKind::DeferStmt:
        deferEvaluationOf(*llvm::cast<DeferStmt>(stmt).expr);
        break;
    case StmtKind::IfStmt:
        emitIfStmt(llvm::cast<IfStmt>(stmt));
        break;
    case StmtKind::SwitchStmt:
        emitSwitchStmt(llvm::cast<SwitchStmt>(stmt));
        break;
    case StmtKind::WhileStmt:
        llvm_unreachable("WhileStmt should be lowered into a ForStmt");
        break;
    case StmtKind::ForStmt:
        emitForStmt(llvm::cast<ForStmt>(stmt));
        break;
    case StmtKind::ForEachStmt:
        llvm_unreachable("ForEachStmt should be lowered into a ForStmt");
        break;
    case StmtKind::BreakStmt:
        emitBreakStmt(llvm::cast<BreakStmt>(stmt));
        break;
    case StmtKind::ContinueStmt:
        emitContinueStmt(llvm::cast<ContinueStmt>(stmt));
        break;
    case StmtKind::CompoundStmt:
        emitCompoundStmt(llvm::cast<CompoundStmt>(stmt));
        break;
    }
}

void IRGenerator::emitStmts(llvm::ArrayRef<Stmt*> stmts) {
    for (Stmt* stmt : stmts) {
        emitStmt(*stmt);
        if (stmt->isReturnStmt() || stmt->isBreakStmt() || stmt->isContinueStmt()) break;
    }
}
