#include "mock_discovery.hpp"

#include "log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <clang/Basic/FileEntry.h>
#include <clang/Basic/SourceManager.h>
#include <filesystem>
#include <fmt/core.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <string>

using namespace clang;
using namespace clang::ast_matchers;

namespace gentest::codegen {
namespace {

using ParamPassStyle = MockParamInfo::PassStyle;

[[nodiscard]] bool is_supported_access(AccessSpecifier access) {
    return access == AS_public || access == AS_protected || access == AS_none;
}

[[nodiscard]] std::string print_type(const QualType &qt, const ASTContext &ctx) {
    auto policy = PrintingPolicy(ctx.getLangOpts());
    policy.adjustForCPlusPlus();
    policy.SuppressScope               = false;
    policy.FullyQualifiedName          = true;
    policy.SuppressUnwrittenScope      = false;
    QualType                 canonical = ctx.getCanonicalType(qt);
    std::string              result;
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
    std::string              result;
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

[[nodiscard]] ParamPassStyle classify_param_pass_style(const ParmVarDecl &param) {
    const QualType type = param.getType();
    if (type->isLValueReferenceType()) {
        return ParamPassStyle::LValueRef;
    }
    if (type->isRValueReferenceType()) {
        const QualType pointee     = type->getPointeeType();
        const bool     unqualified = !pointee.isConstQualified() && !pointee.isVolatileQualified();
        if (unqualified) {
            if (pointee->getAs<TemplateTypeParmType>() != nullptr || pointee->getAs<SubstTemplateTypeParmType>() != nullptr) {
                return ParamPassStyle::ForwardingRef;
            }
            if (const auto *auto_type = pointee->getAs<AutoType>()) {
                if (!auto_type->isDecltypeAuto()) {
                    return ParamPassStyle::ForwardingRef;
                }
            }
        }
        return ParamPassStyle::RValueRef;
    }
    return ParamPassStyle::Value;
}

[[nodiscard]] MockParamInfo build_param_info(const ParmVarDecl &param, const ASTContext &ctx, bool is_template, unsigned index) {
    MockParamInfo info;
    info.type                = is_template ? print_type_as_written(param.getType(), ctx) : print_type(param.getType(), ctx);
    info.pass_style          = classify_param_pass_style(param);
    const QualType base_type = param.getType().getNonReferenceType();
    info.is_const            = base_type.isConstQualified();
    info.is_volatile         = base_type.isVolatileQualified();
    if (!param.getNameAsString().empty()) {
        info.name = param.getNameAsString();
    } else {
        info.name = fmt::format("arg{}", index);
    }
    return info;
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
    // Avoid FunctionProtoType::isNothrow()/canThrow() here: we observed SIGILL
    // in Clang 21 when querying canThrow() for some constructors (special
    // members with unresolved exception specs). For mocking, we only need the
    // *declared* "nothrow-ness" to reproduce the signature; we don't want to
    // evaluate dependent/noexcept expressions.
    switch (method.getExceptionSpecType()) {
    case EST_DynamicNone: // throw()
    case EST_NoThrow:     // MS __declspec(nothrow)
    case EST_BasicNoexcept:
    case EST_NoexceptTrue: return true;
    default: return false;
    }
}

[[nodiscard]] bool has_case_insensitive_suffix(std::string_view path, std::string_view suffix) {
    if (path.size() < suffix.size()) {
        return false;
    }
    const std::size_t start = path.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const auto lhs = static_cast<unsigned char>(path[start + i]);
        const auto rhs = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool looks_like_source_or_module_interface(std::string_view path) {
    // Reject known source/module-interface extensions. Anything else is treated
    // as header-like so nonstandard header names are still supported.
    static constexpr std::array<std::string_view, 16> kRejectedExtensions = {
        ".c", ".cc", ".cp", ".cpp", ".cxx", ".c++", ".m", ".mm", ".cu", ".cppm", ".ccm",
        ".cxxm", ".c++m", ".ixx", ".mxx", ".mpp",
    };
    for (const auto suffix : kRejectedExtensions) {
        if (has_case_insensitive_suffix(path, suffix)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string resolve_definition_file(const SourceManager &sm, SourceLocation loc) {
    const SourceLocation file_loc = sm.getFileLoc(loc);
    std::string          resolved;

    if (file_loc.isValid()) {
        const FileID file_id = sm.getFileID(file_loc);
        if (const auto entry_ref = sm.getFileEntryRefForID(file_id)) {
            const llvm::StringRef real_path = entry_ref->getFileEntry().tryGetRealPathName();
            if (!real_path.empty()) {
                resolved = real_path.str();
            } else {
                resolved = entry_ref->getName().str();
            }
        }
    }
    if (resolved.empty()) {
        resolved = sm.getFilename(file_loc).str();
    }
    if (resolved.empty()) {
        return resolved;
    }

    std::error_code       ec;
    std::filesystem::path p{resolved};
    if (p.is_relative()) {
        const std::filesystem::path abs = std::filesystem::absolute(p, ec);
        if (!ec) {
            p = abs;
        }
    }
    return p.lexically_normal().generic_string();
}

} // namespace

MockUsageCollector::MockUsageCollector(std::vector<MockClassInfo> &out) : out_(out) {}

void MockUsageCollector::report(const SourceManager &sm, SourceLocation loc, std::string_view message) const {
    if (loc.isInvalid()) {
        log_err("gentest_codegen: {}\n", message);
        return;
    }
    const SourceLocation  spelling = sm.getSpellingLoc(loc);
    const llvm::StringRef file     = sm.getFilename(spelling);
    const unsigned        line     = sm.getSpellingLineNumber(spelling);
    if (!file.empty()) {
        log_err("gentest_codegen: {}:{}: {}\n", file.str(), line, message);
    } else {
        log_err("gentest_codegen: {}\n", message);
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
               "gentest::mock<T>: target type is incomplete here; move the interface to a header and include it before the generated mock "
               "registry.");
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

    // Disallow anonymous-namespace and local (function-scope) types: these do not
    // have stable, externally visible qualified names and cannot be safely mocked.
    {
        bool               in_anonymous_ns = false;
        const DeclContext *ctx             = record->getDeclContext();
        while (ctx != nullptr) {
            if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(ctx)) {
                if (ns->isAnonymousNamespace()) {
                    in_anonymous_ns = true;
                    break;
                }
            }
            ctx = ctx->getParent();
        }
        if (in_anonymous_ns) {
            had_error_ = true;
            report(*result.SourceManager, decl.getBeginLoc(),
                   "gentest::mock<T>: cannot mock a type in an anonymous namespace; move it to a named namespace");
            return;
        }
        if (record->isLocalClass()) {
            had_error_ = true;
            report(*result.SourceManager, decl.getBeginLoc(),
                   "gentest::mock<T>: cannot mock a local class defined inside a function; move it to namespace scope");
            return;
        }
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
    info.qualified_name     = record->getQualifiedNameAsString();
    info.display_name       = info.qualified_name;
    info.derive_for_virtual = record->isPolymorphic();
    if (const auto *dtor = record->getDestructor()) {
        info.has_virtual_destructor = dtor->isVirtual();
    } else {
        info.has_virtual_destructor = false;
    }
    info.has_accessible_default_ctor = has_accessible_default_ctor(*record);

    const ASTContext    &ctx                     = *result.Context;
    const SourceManager &sm                      = *result.SourceManager;
    const SourceLocation def_loc                 = sm.getFileLoc(record->getBeginLoc());
    const std::string    definition_file         = resolve_definition_file(sm, def_loc);
    const bool from_named_module_interface = record->isInNamedModule() && !record->isFromHeaderUnit();
    if (definition_file.empty() || looks_like_source_or_module_interface(definition_file) || from_named_module_interface) {
        had_error_                 = true;
        const std::string location = definition_file.empty() ? std::string{"<unknown-file>"} : definition_file;
        report(sm, decl.getBeginLoc(),
               fmt::format("gentest::mock<{}>: target definition must be in a header or header module (found in {})",
                           record->getQualifiedNameAsString(), location));
        return;
    }
    info.definition_file = definition_file;

    // Capture constructors (excluding the default ctor which is tracked via
    // has_accessible_default_ctor). For polymorphic targets, this list is used
    // to generate forwarding constructors so mocks don't require default
    // constructibility.
    llvm::SmallPtrSet<const Decl *, 16> captured_ctors;
    auto                                capture_ctor = [&](const CXXConstructorDecl *ctor) {
        if (ctor == nullptr)
            return;
        if (!captured_ctors.insert(ctor->getCanonicalDecl()).second)
            return;
        if (ctor->isDefaultConstructor())
            return;
        if (ctor->isDeleted())
            return;
        if (!is_supported_access(ctor->getAccess()))
            return;

        MockCtorInfo ctor_info;
        ctor_info.is_explicit = ctor->isExplicit();
        // Preserve declared noexcept-ness so the generated forwarding ctors keep
        // matching the target's signature (e.g. std::is_nothrow_constructible).
        // The helper avoids Clang's canThrow() evaluation (see is_noexcept()).
        ctor_info.is_noexcept = is_noexcept(*ctor);

        if (const auto *ft = ctor->getDescribedFunctionTemplate()) {
            std::string tpl;
            tpl += "template <";
            bool first = true;
            for (const auto *param : *ft->getTemplateParameters()) {
                if (!first)
                    tpl += ", ";
                first = false;
                if (const auto *ttp = llvm::dyn_cast<TemplateTypeParmDecl>(param)) {
                    tpl += (ttp->isParameterPack() ? "typename... " : "typename ");
                    const std::string name = ttp->getNameAsString();
                    tpl += name;
                    ctor_info.template_param_names.push_back(name);
                } else if (const auto *nttp = llvm::dyn_cast<NonTypeTemplateParmDecl>(param)) {
                    tpl += print_type(nttp->getType(), ctx);
                    if (nttp->isParameterPack())
                        tpl += "...";
                    tpl += ' ';
                    const std::string name = nttp->getNameAsString();
                    tpl += name;
                    ctor_info.template_param_names.push_back(name);
                } else if (const auto *tttp = llvm::dyn_cast<TemplateTemplateParmDecl>(param)) {
                    tpl += "template <class...> class ";
                    const std::string name = tttp->getNameAsString();
                    tpl += name;
                    ctor_info.template_param_names.push_back(name);
                } else {
                    tpl += "typename __unk"; // fallback
                }
            }
            tpl += ">";
            ctor_info.template_prefix = std::move(tpl);
        }

        const bool is_template = ctor->getDescribedFunctionTemplate() != nullptr;
        unsigned   arg_index   = 0;
        for (const auto *param : ctor->parameters()) {
            ctor_info.parameters.push_back(build_param_info(*param, ctx, is_template, arg_index));
            ++arg_index;
        }

        info.constructors.push_back(std::move(ctor_info));
    };

    for (const auto *ctor : record->ctors()) {
        capture_ctor(ctor);
    }
    for (const auto *decl : record->decls()) {
        const auto *ft = llvm::dyn_cast<FunctionTemplateDecl>(decl);
        if (ft == nullptr)
            continue;
        capture_ctor(llvm::dyn_cast<CXXConstructorDecl>(ft->getTemplatedDecl()));
    }

    // For polymorphic targets, require that at least one constructor is
    // accessible from the generated mock (default or non-default). Otherwise
    // the mock type would be impossible to instantiate.
    if (info.derive_for_virtual && !info.has_accessible_default_ctor && info.constructors.empty()) {
        had_error_ = true;
        report(*result.SourceManager, decl.getBeginLoc(),
               fmt::format("gentest::mock<{}>: target has no accessible constructors", record->getQualifiedNameAsString()));
        return;
    }

    auto capture_method = [&](const CXXMethodDecl *method) {
        if (llvm::isa<CXXConstructorDecl>(method) || llvm::isa<CXXDestructorDecl>(method)) {
            return;
        }
        if (method->isCopyAssignmentOperator() || method->isMoveAssignmentOperator()) {
            return;
        }
        if (method->isDeleted())
            return;
        if (!is_supported_access(method->getAccess())) {
            return;
        }
        MockMethodInfo method_info;
        method_info.qualified_name = method->getQualifiedNameAsString();
        method_info.method_name    = method->getNameAsString();
        const bool is_template     = method->getDescribedFunctionTemplate() != nullptr;
        method_info.return_type =
            is_template ? print_type_as_written(method->getReturnType(), ctx) : print_type(method->getReturnType(), ctx);
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
                if (!first)
                    tpl += ", ";
                first = false;
                if (const auto *ttp = llvm::dyn_cast<TemplateTypeParmDecl>(param)) {
                    tpl += (ttp->isParameterPack() ? "typename... " : "typename ");
                    const std::string name = ttp->getNameAsString();
                    tpl += name;
                    method_info.template_param_names.push_back(name);
                } else if (const auto *nttp = llvm::dyn_cast<NonTypeTemplateParmDecl>(param)) {
                    tpl += print_type(nttp->getType(), ctx);
                    if (nttp->isParameterPack())
                        tpl += "...";
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
            method_info.parameters.push_back(build_param_info(*param, ctx, is_template, arg_index));
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
    std::ranges::sort(info.methods, {}, &MockMethodInfo::qualified_name);

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
