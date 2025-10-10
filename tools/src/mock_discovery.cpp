#include "mock_discovery.hpp"

#include <algorithm>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceManager.h>
#include <fmt/core.h>
#include <llvm/Support/raw_ostream.h>
#include <string>

using namespace clang;
using namespace clang::ast_matchers;

namespace gentest::codegen {
namespace {

[[nodiscard]] bool is_supported_access(AccessSpecifier access) {
    return access == AS_public || access == AS_protected || access == AS_none;
}

[[nodiscard]] std::string print_type(const QualType &qt, const ASTContext &ctx) {
    auto policy = PrintingPolicy(ctx.getLangOpts());
    policy.adjustForCPlusPlus();
    policy.SuppressScope      = false;
    policy.FullyQualifiedName = true;
    policy.SuppressUnwrittenScope = false;
    QualType canonical             = ctx.getCanonicalType(qt);
    std::string result;
    llvm::raw_string_ostream os(result);
    canonical.print(os, policy);
    os.flush();
    return result;
}

[[nodiscard]] std::string print_type_as_written(const QualType &qt, const ASTContext &ctx) {
    auto policy = PrintingPolicy(ctx.getLangOpts());
    policy.adjustForCPlusPlus();
    policy.SuppressScope          = false;
    policy.FullyQualifiedName     = true;
    policy.SuppressUnwrittenScope = false;
    std::string result;
    llvm::raw_string_ostream os(result);
    qt.print(os, policy);
    os.flush();
    return result;
}

[[nodiscard]] std::string ref_qualifier_string(RefQualifierKind kind) {
    switch (kind) {
    case RQ_LValue: return "&";
    case RQ_RValue: return "&&";
    case RQ_None: break;
    }
    return {};
}

[[nodiscard]] bool has_accessible_default_ctor(const CXXRecordDecl &record) {
    if (!record.hasDefinition())
        return false;
    for (const auto *ctor : record.ctors()) {
        if (!ctor)
            continue;
        if (!ctor->isDefaultConstructor())
            continue;
        if (ctor->isDeleted())
            continue;
        if (is_supported_access(ctor->getAccess()))
            return true;
    }
    if (!record.hasUserDeclaredConstructor()) {
        // Implicit default constructor is generated and has same access as the
        // record (public unless specified otherwise).
        return true;
    }
    return false;
}

[[nodiscard]] bool is_noexcept(const CXXMethodDecl &method) {
    if (const auto *fn = method.getType()->getAs<FunctionProtoType>()) {
        return fn->isNothrow();
    }
    return false;
}

} // namespace

MockUsageCollector::MockUsageCollector(std::vector<MockClassInfo> &out) : out_(out) {}

void MockUsageCollector::report(const SourceManager &sm, SourceLocation loc, std::string_view message) const {
    if (loc.isInvalid()) {
        llvm::errs() << fmt::format("gentest_codegen: {}\n", message);
        return;
    }
    const SourceLocation  spelling = sm.getSpellingLoc(loc);
    const llvm::StringRef file     = sm.getFilename(spelling);
    const unsigned        line     = sm.getSpellingLineNumber(spelling);
    if (!file.empty()) {
        llvm::errs() << fmt::format("gentest_codegen: {}:{}: {}\n", file.str(), line, message);
    } else {
        llvm::errs() << fmt::format("gentest_codegen: {}\n", message);
    }
}

void MockUsageCollector::handle_specialization(const ClassTemplateSpecializationDecl &decl, const MatchFinder::MatchResult &result) {
    if (decl.getTemplateArgs().size() == 0) {
        had_error_ = true;
        report(*result.SourceManager, decl.getBeginLoc(), "gentest::mock requires at least one template argument");
        return;
    }

    const TemplateArgument &first = decl.getTemplateArgs().get(0);
    if (first.getKind() != TemplateArgument::Type) {
        had_error_ = true;
        report(*result.SourceManager, decl.getBeginLoc(), "gentest::mock expects a type argument");
        return;
    }

    QualType target_type = first.getAsType();
    if (target_type.isNull()) {
        had_error_ = true;
        report(*result.SourceManager, decl.getBeginLoc(), "gentest::mock argument resolves to an invalid type");
        return;
    }

    const auto *record = target_type->getAsCXXRecordDecl();
    if (record == nullptr) {
        had_error_ = true;
        report(*result.SourceManager, decl.getBeginLoc(), "gentest::mock argument is not a class or struct type");
        return;
    }

    record = record->getDefinition();
    if (record == nullptr) {
        had_error_ = true;
        report(*result.SourceManager, decl.getBeginLoc(),
               "gentest::mock<T>: target type is incomplete here; move the interface to a header and include it before the generated mock registry.");
        return;
    }

    const auto *canonical = record->getCanonicalDecl();
    if (!seen_.insert(canonical).second) {
        return;
    }

    if (record->isUnion()) {
        had_error_ = true;
        report(*result.SourceManager, decl.getBeginLoc(), "gentest::mock does not support union types");
        return;
    }

    if (record->hasAttr<FinalAttr>() || record->isEffectivelyFinal()) {
        had_error_ = true;
        report(*result.SourceManager, decl.getBeginLoc(), "gentest::mock cannot mock a final class");
        return;
    }

    const auto *dtor = record->getDestructor();
    if (dtor && dtor->getAccess() == AS_private) {
        had_error_ = true;
        report(*result.SourceManager, decl.getBeginLoc(), "gentest::mock requires a non-private destructor");
        return;
    }

    MockClassInfo info;
    info.qualified_name              = record->getQualifiedNameAsString();
    info.display_name                = info.qualified_name;
    info.derive_for_virtual          = record->isPolymorphic();
    if (const auto *dtor = record->getDestructor()) {
        info.has_virtual_destructor = dtor->isVirtual();
    } else {
        info.has_virtual_destructor = false;
    }
    info.has_accessible_default_ctor = has_accessible_default_ctor(*record);

    if (!info.has_accessible_default_ctor) {
        had_error_ = true;
        report(*result.SourceManager, decl.getBeginLoc(),
               fmt::format("gentest::mock requires '{}' to have an accessible default constructor", info.display_name));
        return;
    }

    const ASTContext &ctx = *result.Context;

    // Polymorphic (virtual) types must have their interface visible from a header.
    if (info.derive_for_virtual) {
        const SourceLocation def_loc = record->getBeginLoc();
        const SourceManager &sm      = *result.SourceManager;
        const auto           file    = sm.getFilename(sm.getFileLoc(def_loc));
        auto                  ends_with = [](llvm::StringRef s, llvm::StringRef suf) { return s.size() >= suf.size() && s.ends_with(suf); };
        const bool           looks_like_source =
            (!file.empty() && (ends_with(file, ".cc") || ends_with(file, ".cpp") || ends_with(file, ".cxx")));
        const bool in_main = sm.isWrittenInMainFile(def_loc);
        if (looks_like_source || in_main) {
            had_error_ = true;
            report(sm, def_loc,
                   fmt::format("gentest::mock<{}>: polymorphic target appears defined in a source file ({}); move the interface to a header included before the generated mock registry",
                               record->getQualifiedNameAsString(), file.str()));
            return;
        }
    }

    auto capture_method = [&](const CXXMethodDecl* method) {
        if (llvm::isa<CXXConstructorDecl>(method) || llvm::isa<CXXDestructorDecl>(method)) {
            return;
        }
        if (method->isCopyAssignmentOperator() || method->isMoveAssignmentOperator()) {
            return;
        }
        if (method->isDeleted())
            return;
        if (method->isStatic())
            return; // static members currently unsupported
        if (!is_supported_access(method->getAccess())) {
            return;
        }
        MockMethodInfo method_info;
        method_info.qualified_name  = method->getQualifiedNameAsString();
        method_info.method_name     = method->getNameAsString();
        const bool is_template      = method->getDescribedFunctionTemplate() != nullptr;
        method_info.return_type     = is_template ? print_type_as_written(method->getReturnType(), ctx)
                                                  : print_type(method->getReturnType(), ctx);
        method_info.is_const        = method->isConst();
        method_info.is_volatile     = method->isVolatile();
        method_info.is_static       = method->isStatic();
        method_info.is_virtual      = method->isVirtual();
        method_info.is_pure_virtual = method->isPureVirtual();
        method_info.is_noexcept     = is_noexcept(*method);
        method_info.ref_qualifier   = ref_qualifier_string(method->getRefQualifier());

        if (const auto *ft = method->getDescribedFunctionTemplate()) {
            std::string tpl;
            tpl += "template <";
            bool first = true;
            for (const auto *param : *ft->getTemplateParameters()) {
                if (!first) tpl += ", ";
                first = false;
                if (const auto *ttp = llvm::dyn_cast<TemplateTypeParmDecl>(param)) {
                    tpl += (ttp->isParameterPack() ? "typename... " : "typename ");
                    const std::string name = ttp->getNameAsString();
                    tpl += name;
                    method_info.template_param_names.push_back(name);
                } else if (const auto *nttp = llvm::dyn_cast<NonTypeTemplateParmDecl>(param)) {
                    tpl += print_type(nttp->getType(), ctx);
                    if (nttp->isParameterPack()) tpl += "...";
                    tpl += ' ';
                    const std::string name = nttp->getNameAsString();
                    tpl += name;
                    method_info.template_param_names.push_back(name);
                } else if (const auto *tttp = llvm::dyn_cast<TemplateTemplateParmDecl>(param)) {
                    tpl += "template <class...> class ";
                    const std::string name = tttp->getNameAsString();
                    tpl += name;
                    method_info.template_param_names.push_back(name);
                } else {
                    tpl += "typename __unk"; // fallback
                }
            }
            tpl += ">";
            method_info.template_prefix = std::move(tpl);
        }

        unsigned arg_index = 0;
        for (const auto *param : method->parameters()) {
            MockParamInfo param_info;
            param_info.type = is_template ? print_type_as_written(param->getType(), ctx)
                                         : print_type(param->getType(), ctx);
            if (!param->getNameAsString().empty()) {
                param_info.name = param->getNameAsString();
            } else {
                param_info.name = fmt::format("arg{}", arg_index);
            }
            method_info.parameters.push_back(std::move(param_info));
            ++arg_index;
        }

        info.methods.push_back(std::move(method_info));
    };

    for (const auto *method : record->methods()) {
        capture_method(method);
    }

    // Also capture function template declarations (non-virtual in practice).
    for (const auto *decl : record->decls()) {
        if (const auto *ft = llvm::dyn_cast<FunctionTemplateDecl>(decl)) {
            if (const auto *templated = llvm::dyn_cast<CXXMethodDecl>(ft->getTemplatedDecl())) {
                capture_method(templated);
            }
        }
    }

    // Stable order for deterministic output.
    std::sort(info.methods.begin(), info.methods.end(),
              [](const MockMethodInfo &lhs, const MockMethodInfo &rhs) { return lhs.qualified_name < rhs.qualified_name; });

    out_.push_back(std::move(info));
}

void MockUsageCollector::run(const MatchFinder::MatchResult &result) {
    if (const auto *spec = result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("gentest.mock")) {
        handle_specialization(*spec, result);
    }
}

bool MockUsageCollector::has_errors() const { return had_error_; }

void register_mock_matchers(MatchFinder &finder, MockUsageCollector &collector) {
    const auto matcher = classTemplateSpecializationDecl(hasName("gentest::mock")).bind("gentest.mock");
    finder.addMatcher(matcher, &collector);
}

} // namespace gentest::codegen
