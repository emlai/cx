#pragma once

#include <string>
#include <memory>
#include <cassert>
#include <boost/variant.hpp>

class Expr;

enum class ExprKind {
    VariableExpr,
    IntLiteralExpr,
    PrefixExpr,
    BinaryExpr,
};

struct VariableExpr {
    std::string identifier;
};

struct IntLiteralExpr {
    int64_t value;
};

struct PrefixExpr {
    char op;
    std::unique_ptr<Expr> operand;
};

struct BinaryExpr {
    char op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

class Expr {
public:
#define DEFINE_EXPRKIND_GETTER_AND_CONSTRUCTOR(KIND) \
    Expr(KIND&& value) : data(std::move(value)) { } \
    \
    KIND& get##KIND() { \
        assert(getKind() == ExprKind::KIND); \
        return boost::get<KIND>(data); \
    } \
    const KIND& get##KIND() const { \
        assert(getKind() == ExprKind::KIND); \
        return boost::get<KIND>(data); \
    }
    DEFINE_EXPRKIND_GETTER_AND_CONSTRUCTOR(VariableExpr)
    DEFINE_EXPRKIND_GETTER_AND_CONSTRUCTOR(IntLiteralExpr)
    DEFINE_EXPRKIND_GETTER_AND_CONSTRUCTOR(PrefixExpr)
    DEFINE_EXPRKIND_GETTER_AND_CONSTRUCTOR(BinaryExpr)
#undef DEFINE_EXPRKIND_GETTER_AND_CONSTRUCTOR

    Expr(Expr&& expr) : data(std::move(expr.data)) { }
    ExprKind getKind() const { return static_cast<ExprKind>(data.which()); }

private:
    boost::variant<VariableExpr, IntLiteralExpr, PrefixExpr, BinaryExpr> data;
};
