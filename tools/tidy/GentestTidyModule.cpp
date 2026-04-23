#include "parse.hpp"
#include "validate.hpp"

#include <clang-tidy/ClangTidy.h>
#include <clang-tidy/ClangTidyCheck.h>
#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTTypeTraits.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/StmtCXX.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Tooling/Tooling.h>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace gentest::tidy {

namespace {

std::string qualified_name(const NamedDecl &decl) { return decl.getQualifiedNameAsString(); }

bool qualified_name_is(const NamedDecl &decl, std::string_view expected) {
    std::string name = qualified_name(decl);
    if (name == expected) {
        return true;
    }
    if (!name.starts_with("::")) {
        name.insert(0, "::");
    }
    return name == expected;
}

bool decl_context_is_in_namespace(const DeclContext *context, std::string_view namespace_name) {
    const llvm::StringRef expected(namespace_name.data(), namespace_name.size());
    for (const DeclContext *current = context; current != nullptr; current = current->getParent()) {
        if (const auto *ns = dyn_cast<NamespaceDecl>(current); ns && ns->getName() == expected) {
            return true;
        }
    }
    return false;
}

bool is_std_thread_record(const CXXRecordDecl *record) {
    if (!record) {
        return false;
    }
    if (const auto *definition = record->getDefinition()) {
        record = definition;
    }
    const auto name = record->getName();
    return (name == "thread" || name == "jthread") && decl_context_is_in_namespace(record->getDeclContext(), "std");
}

bool is_std_thread_type(QualType type) {
    type = type.getCanonicalType();
    return is_std_thread_record(type->getAsCXXRecordDecl());
}

bool type_text_mentions_std_thread(QualType type) {
    const std::string text = type.getAsString();
    return text.find("std::thread") != std::string::npos || text.find("std::jthread") != std::string::npos ||
           text.find("std::__1::thread") != std::string::npos || text.find("std::__1::jthread") != std::string::npos;
}

bool is_std_launch_enum(QualType type) {
    type                  = type.getCanonicalType();
    const auto *enum_type = type->getAs<EnumType>();
    if (!enum_type) {
        return false;
    }
    const EnumDecl *decl = enum_type->getDecl();
    return decl && decl->getName() == "launch" && decl_context_is_in_namespace(decl->getDeclContext(), "std");
}

std::optional<std::uint64_t> launch_enumerator_value(const EnumDecl &decl, llvm::StringRef name) {
    for (const EnumConstantDecl *enumerator : decl.enumerators()) {
        if (enumerator && enumerator->getName() == name) {
            return static_cast<std::uint64_t>(enumerator->getInitVal().getZExtValue());
        }
    }
    return std::nullopt;
}

bool launch_policy_allows_async(const Expr &policy, ASTContext &context) {
    const Expr *policy_expr = policy.IgnoreParenImpCasts();
    if (!policy_expr || !is_std_launch_enum(policy_expr->getType())) {
        return true;
    }

    const auto *enum_type = policy_expr->getType().getCanonicalType()->getAs<EnumType>();
    if (!enum_type) {
        return true;
    }
    auto async_mask = launch_enumerator_value(*enum_type->getDecl(), "async");
    if (!async_mask) {
        return true;
    }

    Expr::EvalResult evaluated{};
    if (!policy_expr->EvaluateAsInt(evaluated, context)) {
        return true;
    }
    return (static_cast<std::uint64_t>(evaluated.Val.getInt().getZExtValue()) & *async_mask) != 0U;
}

bool is_std_async_call(const CallExpr &call, ASTContext &context) {
    const FunctionDecl *callee = call.getDirectCallee();
    if (!callee) {
        return false;
    }
    if (callee->getName() != "async" || !decl_context_is_in_namespace(callee->getDeclContext(), "std")) {
        return false;
    }
    if (call.getNumArgs() == 0 || !is_std_launch_enum(call.getArg(0)->IgnoreParenImpCasts()->getType())) {
        return true;
    }
    return launch_policy_allows_async(*call.getArg(0), context);
}

bool is_std_thread_container_emplace(const CallExpr &call) {
    const auto *member_call = dyn_cast<CXXMemberCallExpr>(&call);
    if (!member_call) {
        return false;
    }
    const CXXMethodDecl *method = member_call->getMethodDecl();
    if (!method) {
        return false;
    }
    const auto method_name = method->getName();
    if (method_name != "emplace_back" && method_name != "push_back") {
        return false;
    }
    return type_text_mentions_std_thread(member_call->getObjectType());
}

bool is_thread_construct_expr(const CXXConstructExpr &construct) {
    if (is_std_thread_type(construct.getType())) {
        return true;
    }
    const CXXConstructorDecl *ctor = construct.getConstructor();
    return ctor && is_std_thread_record(ctor->getParent());
}

bool expr_is_used_in_thread_like_context(const Expr &expr, ASTContext &context) {
    DynTypedNode current = DynTypedNode::create(expr);
    for (int depth = 0; depth < 32; ++depth) {
        const auto parents = context.getParents(current);
        if (parents.empty()) {
            return false;
        }
        current = *parents.begin();

        if (const auto *construct = current.get<CXXConstructExpr>(); construct && is_thread_construct_expr(*construct)) {
            return true;
        }
        if (const auto *call = current.get<CallExpr>();
            call && (is_std_async_call(*call, context) || is_std_thread_container_emplace(*call))) {
            return true;
        }
        if (current.get<FunctionDecl>() || current.get<VarDecl>() || current.get<LambdaExpr>()) {
            return false;
        }
    }
    return false;
}

class ThreadLikeCallableUseVisitor : public RecursiveASTVisitor<ThreadLikeCallableUseVisitor> {
  public:
    ThreadLikeCallableUseVisitor(const NamedDecl &target, ASTContext &context) : target_(target.getCanonicalDecl()), context_(context) {}

    bool VisitDeclRefExpr(DeclRefExpr *expr) {
        const auto *decl = dyn_cast_or_null<NamedDecl>(expr ? expr->getDecl() : nullptr);
        if (!decl || decl->getCanonicalDecl() != target_) {
            return true;
        }
        if (expr_is_used_in_thread_like_context(*expr, context_)) {
            found_ = true;
            return false;
        }
        return true;
    }

    bool found() const { return found_; }

  private:
    const Decl *target_ = nullptr;
    ASTContext &context_;
    bool        found_ = false;
};

bool callable_decl_is_used_in_thread_like_context(const NamedDecl &decl, ASTContext &context) {
    ThreadLikeCallableUseVisitor visitor(decl, context);
    visitor.TraverseDecl(context.getTranslationUnitDecl());
    return visitor.found();
}

bool is_async_execution_lambda(const LambdaExpr &lambda, ASTContext &context) {
    DynTypedNode current = DynTypedNode::create(lambda);
    for (int depth = 0; depth < 24; ++depth) {
        const auto parents = context.getParents(current);
        if (parents.empty()) {
            return false;
        }
        current = *parents.begin();

        if (const auto *construct = current.get<CXXConstructExpr>(); construct && is_thread_construct_expr(*construct)) {
            return true;
        }
        if (const auto *call = current.get<CallExpr>();
            call && (is_std_async_call(*call, context) || is_std_thread_container_emplace(*call))) {
            return true;
        }
        if (const auto *var = current.get<VarDecl>()) {
            return callable_decl_is_used_in_thread_like_context(*var, context);
        }
        if (current.get<FunctionDecl>()) {
            return false;
        }
    }
    return false;
}

bool is_set_current_token_call(const CallExpr &call) {
    const FunctionDecl *callee = call.getDirectCallee();
    return callee && qualified_name_is(*callee, "gentest::set_current_token");
}

bool is_gentest_adoption_type(QualType type) {
    type                        = type.getNonReferenceType().getUnqualifiedType().getCanonicalType();
    const CXXRecordDecl *record = type->getAsCXXRecordDecl();
    return record && qualified_name_is(*record, "gentest::Adoption");
}

bool is_gentest_runtime_call(const CallExpr &call) {
    const FunctionDecl *callee = call.getDirectCallee();
    if (!callee) {
        return false;
    }

    static constexpr std::string_view kRuntimeNames[] = {
        "gentest::log",
        "gentest::logf",
        "gentest::fail",
        "gentest::skip",
        "gentest::skip_if",
        "gentest::xfail",
        "gentest::xfail_if",
        "gentest::expect",
        "gentest::expect_true",
        "gentest::expect_false",
        "gentest::expect_eq",
        "gentest::expect_ne",
        "gentest::expect_lt",
        "gentest::expect_le",
        "gentest::expect_gt",
        "gentest::expect_ge",
        "gentest::require",
        "gentest::require_false",
        "gentest::require_eq",
        "gentest::require_ne",
        "gentest::require_lt",
        "gentest::require_le",
        "gentest::require_gt",
        "gentest::require_ge",
        "gentest::assert_true",
        "gentest::assert_false",
        "gentest::assert_eq",
        "gentest::asserts::EXPECT_TRUE",
        "gentest::asserts::EXPECT_FALSE",
        "gentest::asserts::EXPECT_EQ",
        "gentest::asserts::EXPECT_NE",
        "gentest::asserts::EXPECT_LT",
        "gentest::asserts::EXPECT_LE",
        "gentest::asserts::EXPECT_GT",
        "gentest::asserts::EXPECT_GE",
        "gentest::asserts::ASSERT_TRUE",
        "gentest::asserts::ASSERT_FALSE",
        "gentest::asserts::ASSERT_EQ",
        "gentest::asserts::ASSERT_NE",
        "gentest::asserts::ASSERT_LT",
        "gentest::asserts::ASSERT_LE",
        "gentest::asserts::ASSERT_GT",
        "gentest::asserts::ASSERT_GE",
        "gentest::asserts::EXPECT_THROW",
        "gentest::asserts::EXPECT_NO_THROW",
        "gentest::asserts::ASSERT_THROW",
        "gentest::asserts::ASSERT_NO_THROW",
        "gentest::detail::record_failure",
        "gentest::detail::expect_throw",
        "gentest::detail::expect_no_throw",
        "gentest::detail::require_throw",
        "gentest::detail::require_no_throw",
    };

    for (std::string_view name : kRuntimeNames) {
        if (qualified_name_is(*callee, name)) {
            return true;
        }
    }
    return false;
}

std::string gentest_runtime_call_name(const CallExpr &call) {
    const FunctionDecl *callee = call.getDirectCallee();
    if (!callee) {
        return "gentest runtime API";
    }
    std::string name = qualified_name(*callee);
    if (name.empty()) {
        return "gentest runtime API";
    }
    return name;
}

bool stmt_is_ancestor_of(const Stmt *ancestor, const Stmt *descendant, ASTContext &context) {
    if (!ancestor || !descendant) {
        return false;
    }
    if (ancestor == descendant) {
        return true;
    }

    std::vector<DynTypedNode> pending{DynTypedNode::create(*descendant)};
    for (std::size_t cursor = 0; cursor < pending.size() && cursor < 128; ++cursor) {
        for (const DynTypedNode &parent : context.getParents(pending[cursor])) {
            if (const auto *stmt = parent.get<Stmt>(); stmt == ancestor) {
                return true;
            }
            pending.push_back(parent);
        }
    }
    return false;
}

const Stmt *declaration_lifetime_owner(const VarDecl &var, ASTContext &context) {
    DynTypedNode current = DynTypedNode::create(var);
    for (int depth = 0; depth < 64; ++depth) {
        const auto parents = context.getParents(current);
        if (parents.empty()) {
            return nullptr;
        }
        current = *parents.begin();
        if (const auto *stmt = current.get<Stmt>()) {
            if (isa<CompoundStmt>(stmt) || isa<IfStmt>(stmt) || isa<SwitchStmt>(stmt) || isa<ForStmt>(stmt) || isa<CXXForRangeStmt>(stmt) ||
                isa<WhileStmt>(stmt)) {
                return stmt;
            }
            if (isa<LambdaExpr>(stmt)) {
                return nullptr;
            }
        }
        if (current.get<FunctionDecl>()) {
            return nullptr;
        }
    }
    return nullptr;
}

class SetCurrentTokenCallFinder : public RecursiveASTVisitor<SetCurrentTokenCallFinder> {
  public:
    bool VisitCallExpr(CallExpr *call) {
        if (call && is_set_current_token_call(*call)) {
            found_ = true;
            return false;
        }
        return true;
    }

    bool found() const { return found_; }

  private:
    bool found_ = false;
};

bool initializes_adoption_from_current_token(const VarDecl &var) {
    if (!is_gentest_adoption_type(var.getType())) {
        return false;
    }
    const Expr *init = var.getInit();
    if (!init) {
        return false;
    }
    SetCurrentTokenCallFinder finder;
    finder.TraverseStmt(const_cast<Expr *>(init));
    return finder.found();
}

class ActiveTokenAdoptionVisitor : public RecursiveASTVisitor<ActiveTokenAdoptionVisitor> {
  public:
    ActiveTokenAdoptionVisitor(ASTContext &context, const CallExpr &target, SourceLocation before,
                               bool require_adoption_after_latest_suspend)
        : context_(context), target_(target), before_(before),
          require_adoption_after_latest_suspend_(require_adoption_after_latest_suspend) {}

    bool VisitDeclStmt(DeclStmt *decl_stmt) {
        if (!decl_stmt) {
            return true;
        }

        SourceLocation decl_loc = context_.getSourceManager().getExpansionLoc(decl_stmt->getBeginLoc());
        if (!is_before_target(decl_loc)) {
            return true;
        }

        for (Decl *decl : decl_stmt->decls()) {
            const auto *var = dyn_cast_or_null<VarDecl>(decl);
            if (!var || !initializes_adoption_from_current_token(*var)) {
                continue;
            }
            SourceLocation decl_loc = context_.getSourceManager().getExpansionLoc(var->getBeginLoc());
            if (!is_before_target(decl_loc)) {
                continue;
            }
            if (require_adoption_after_latest_suspend_ && latest_suspend_.isValid() &&
                !context_.getSourceManager().isBeforeInTranslationUnit(latest_suspend_, decl_loc)) {
                continue;
            }
            const Stmt *scope = declaration_lifetime_owner(*var, context_);
            if (scope && stmt_is_ancestor_of(scope, &target_, context_)) {
                record_latest(adoption_, decl_loc);
            }
        }
        return true;
    }

    bool VisitCoawaitExpr(CoawaitExpr *expr) {
        if (expr) {
            record_suspend(expr->getBeginLoc());
        }
        return true;
    }

    bool VisitCoyieldExpr(CoyieldExpr *expr) {
        if (expr) {
            record_suspend(expr->getBeginLoc());
        }
        return true;
    }

    bool found() const {
        if (adoption_.isInvalid()) {
            return false;
        }
        if (!require_adoption_after_latest_suspend_ || latest_suspend_.isInvalid()) {
            return true;
        }
        return context_.getSourceManager().isBeforeInTranslationUnit(latest_suspend_, adoption_);
    }

  private:
    bool is_before_target(SourceLocation loc) const {
        loc = context_.getSourceManager().getExpansionLoc(loc);
        return loc.isValid() && before_.isValid() && context_.getSourceManager().isBeforeInTranslationUnit(loc, before_);
    }

    void record_latest(SourceLocation &current, SourceLocation candidate) const {
        candidate = context_.getSourceManager().getExpansionLoc(candidate);
        if (!is_before_target(candidate)) {
            return;
        }
        if (current.isInvalid() || context_.getSourceManager().isBeforeInTranslationUnit(current, candidate)) {
            current = candidate;
        }
    }

    void record_suspend(SourceLocation loc) { record_latest(latest_suspend_, loc); }

    ASTContext     &context_;
    const CallExpr &target_;
    SourceLocation  before_;
    bool            require_adoption_after_latest_suspend_ = false;
    SourceLocation  adoption_{};
    SourceLocation  latest_suspend_{};
};

bool has_prior_token_adoption(const Stmt &body, const CallExpr &target, SourceLocation before, ASTContext &context,
                              bool require_adoption_after_latest_suspend) {
    ActiveTokenAdoptionVisitor visitor(context, target, before, require_adoption_after_latest_suspend);
    visitor.TraverseStmt(const_cast<Stmt *>(&body));
    return visitor.found();
}

struct RiskyExecutionContext {
    const Stmt *body = nullptr;
    std::string kind;
    bool        require_adoption_after_latest_suspend = false;
};

std::optional<RiskyExecutionContext> find_risky_execution_context(const CallExpr &call, ASTContext &context) {
    DynTypedNode current = DynTypedNode::create(call);
    for (int depth = 0; depth < 64; ++depth) {
        const auto parents = context.getParents(current);
        if (parents.empty()) {
            return std::nullopt;
        }
        current = *parents.begin();

        if (const auto *lambda = current.get<LambdaExpr>(); lambda && is_async_execution_lambda(*lambda, context)) {
            return RiskyExecutionContext{.body = lambda->getBody(), .kind = "thread-like callback"};
        }
        if (const auto *coroutine_body = current.get<CoroutineBodyStmt>()) {
            return RiskyExecutionContext{.body = coroutine_body, .kind = "coroutine body", .require_adoption_after_latest_suspend = true};
        }
        if (const auto *function = current.get<FunctionDecl>(); function) {
            if (const auto *method = dyn_cast<CXXMethodDecl>(function); method && method->getParent() && method->getParent()->isLambda()) {
                continue;
            }
            const Stmt *body = function->getBody();
            if (body && isa<CoroutineBodyStmt>(body)) {
                return RiskyExecutionContext{.body = body, .kind = "coroutine body", .require_adoption_after_latest_suspend = true};
            }
            if (body && callable_decl_is_used_in_thread_like_context(*function, context)) {
                return RiskyExecutionContext{.body = body, .kind = "thread-like callback"};
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace

class GentestAttributesCheck : public clang::tidy::ClangTidyCheck {
  public:
    GentestAttributesCheck(llvm::StringRef Name, clang::tidy::ClangTidyContext *Context) : clang::tidy::ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        Finder->addMatcher(functionDecl(isDefinition()).bind("func"), this);
        Finder->addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit())).bind("record"), this);
        Finder->addMatcher(namespaceDecl().bind("ns"), this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        const auto *SM = Result.SourceManager;
        if (!SM)
            return;

        auto emit_collection_diags = [&](SourceLocation loc, const gentest::codegen::AttributeCollection &collected) {
            for (const auto &message : collected.mis_scoped_gentest) {
                diag(loc, "attribute '%0' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification") << message;
            }

            for (const auto &message : collected.other_namespaces) {
                diag(loc, "attribute '%0' ignored (unsupported attribute namespace)") << message;
            }
        };

        if (const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("func")) {
            const auto collected = gentest::codegen::collect_gentest_attributes_for(*FD, *SM);
            emit_collection_diags(FD->getBeginLoc(), collected);
            if (collected.gentest.empty())
                return;

            auto summary =
                gentest::codegen::validate_attributes(collected.gentest, [&](const std::string &m) { diag(FD->getBeginLoc(), m); });
            (void)summary;
            return;
        }

        if (const auto *RD = Result.Nodes.getNodeAs<CXXRecordDecl>("record")) {
            const auto collected = gentest::codegen::collect_gentest_attributes_for(*RD, *SM);
            emit_collection_diags(RD->getBeginLoc(), collected);
            if (collected.gentest.empty())
                return;

            auto summary =
                gentest::codegen::validate_fixture_attributes(collected.gentest, [&](const std::string &m) { diag(RD->getBeginLoc(), m); });
            (void)summary;
            return;
        }

        if (const auto *NS = Result.Nodes.getNodeAs<NamespaceDecl>("ns")) {
            SourceLocation loc = NS->getBeginLoc();
            if (loc.isInvalid())
                loc = NS->getLocation();

            const auto collected = gentest::codegen::collect_gentest_attributes_for(*NS, *SM);
            emit_collection_diags(loc, collected);
            if (collected.gentest.empty())
                return;

            auto summary = gentest::codegen::validate_namespace_attributes(collected.gentest, [&](const std::string &m) { diag(loc, m); });
            (void)summary;
        }
    }
};

class GentestTokenAdoptionCheck : public clang::tidy::ClangTidyCheck {
  public:
    GentestTokenAdoptionCheck(llvm::StringRef Name, clang::tidy::ClangTidyContext *Context) : clang::tidy::ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        Finder->addMatcher(callExpr(callee(functionDecl())).bind("gentest-runtime-call"), this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        const auto *runtime_call = Result.Nodes.getNodeAs<CallExpr>("gentest-runtime-call");
        ASTContext *context      = Result.Context;
        const auto *SM           = Result.SourceManager;
        if (!runtime_call || !context || !SM || !is_gentest_runtime_call(*runtime_call)) {
            return;
        }

        SourceLocation call_loc = SM->getExpansionLoc(runtime_call->getBeginLoc());
        if (call_loc.isInvalid()) {
            return;
        }

        auto risky_context = find_risky_execution_context(*runtime_call, *context);
        if (!risky_context || !risky_context->body) {
            return;
        }
        if (has_prior_token_adoption(*risky_context->body, *runtime_call, call_loc, *context,
                                     risky_context->require_adoption_after_latest_suspend)) {
            return;
        }

        diag(call_loc, "gentest runtime call %0 in %1 may run without an active current token; keep the gentest::Adoption returned by "
                       "gentest::set_current_token() alive in this scope")
            << gentest_runtime_call_name(*runtime_call) << risky_context->kind;
    }
};

class GentestTidyModule : public clang::tidy::ClangTidyModule {
  public:
    void addCheckFactories(clang::tidy::ClangTidyCheckFactories &Factories) override {
        Factories.registerCheck<GentestAttributesCheck>("gentest-attributes");
        Factories.registerCheck<GentestTokenAdoptionCheck>("gentest-token-adoption");
    }
};

} // namespace gentest::tidy

// NOLINTNEXTLINE(cert-err58-cpp)
static clang::tidy::ClangTidyModuleRegistry::Add<gentest::tidy::GentestTidyModule> X("gentest-module",
                                                                                     "Gentest attributes validation checks");

// This anchor is used to force the linker to link in the generated object file
// and thus register the module.
volatile int GentestTidyModuleAnchorSource = 0;
