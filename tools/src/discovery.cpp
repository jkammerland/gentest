// Implementation of AST discovery + validation

#include "discovery.hpp"

#include "discovery_utils.hpp"
#include "log.hpp"
#include "parse.hpp"
#include "render.hpp"
#include "type_kind.hpp"
#include "validate.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ODRHash.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/QualTypeNames.h>
#include <clang/Basic/Module.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Version.h>
#include <clang/Sema/Sema.h>
#if __has_include(<clang/UnifiedSymbolResolution/USRGeneration.h>)
#include <clang/UnifiedSymbolResolution/USRGeneration.h>
#else
#include <clang/Index/USRGeneration.h>
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>
#include <iterator>
#include <limits>
#include <llvm/ADT/SmallBitVector.h>
#include <llvm/ADT/SmallString.h>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using namespace clang;
using namespace clang::ast_matchers;
using gentest::codegen::classify_type;
using gentest::codegen::quote_for_type;
using gentest::codegen::TypeKind;

namespace gentest::codegen {

namespace {
std::string source_file_identity(const SourceManager &sm, SourceLocation location) {
    const SourceLocation file_location = sm.getFileLoc(location);
    if (file_location.isInvalid()) {
        return "<invalid>";
    }

    const FileID file_id = sm.getFileID(file_location);
    if (const auto *entry = sm.getFileEntryForID(file_id); entry != nullptr) {
        const auto uid = entry->getUniqueID();
        return fmt::format("{}:{}", uid.getDevice(), uid.getFile());
    }
    return sm.getFilename(file_location).str();
}

std::string source_location_identity(const SourceManager &sm, SourceLocation location) {
    const SourceLocation file_location = sm.getFileLoc(location);
    if (file_location.isInvalid()) {
        return "<invalid>:0";
    }
    return fmt::format("{}:{}", source_file_identity(sm, file_location), sm.getFileOffset(file_location));
}

std::string declaration_site_identity(const NamedDecl &decl, const SourceManager &sm, std::string_view declaration_kind) {
    const SourceLocation name_location = decl.getLocation();
    std::string          key =
        fmt::format("{}|expansion:{}|spelling:{}", declaration_kind, source_location_identity(sm, sm.getExpansionLoc(name_location)),
                    source_location_identity(sm, sm.getSpellingLoc(name_location)));

    // Expansion and spelling alone are insufficient when nested macros reuse
    // the same declaration-producing tokens. Retain every invocation/caller
    // boundary so repeated invocations and multi-declaration macros stay
    // distinct while the same tokens seen from another TU compare equal.
    SourceLocation cursor = name_location;
    std::size_t    depth  = 0;
    while (cursor.isMacroID()) {
        const SourceLocation caller = sm.getImmediateMacroCallerLoc(cursor);
        fmt::format_to(std::back_inserter(key), "|macro{}:{}:{}", depth, source_location_identity(sm, sm.getSpellingLoc(cursor)),
                       source_location_identity(sm, sm.getExpansionLoc(cursor)));
        if (caller.isInvalid() || caller == cursor) {
            break;
        }
        cursor = caller;
        ++depth;
    }
    return key;
}

const Module *owning_linkage_module(const Decl &decl) { return decl.getOwningModuleForLinkage(); }

std::string owning_module_identity(const Decl &decl) {
    // getTopLevelOwningNamedModule() is newer than the oldest supported
    // libTooling. The linkage owner is specifically a standard C++ named
    // module on every supported Clang, unlike getOwningModule(), which can
    // also name a Clang header module.
    if (const Module *module = owning_linkage_module(decl); module != nullptr) {
        std::string name = module->getFullModuleName();
        if (const auto partition = name.find(':'); partition != std::string::npos) {
            name.resize(partition);
        }
        return name;
    }
    return {};
}

bool is_importer_reachable_entity(const NamedDecl &decl) {
    const auto exported = [](const NamedDecl *candidate) { return candidate != nullptr && candidate->isInExportDeclContext(); };

    if (const auto *function = llvm::dyn_cast<FunctionDecl>(&decl)) {
        if (std::ranges::any_of(function->redecls(), exported)) {
            return true;
        }
        if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(function); method != nullptr) {
            const auto *parent = method->getParent();
            if (parent != nullptr && std::ranges::any_of(parent->redecls(), exported)) {
                return true;
            }
        }
        return false;
    }
    if (const auto *tag = llvm::dyn_cast<TagDecl>(&decl)) {
        return std::ranges::any_of(tag->redecls(), exported);
    }
    return exported(&decl) || exported(llvm::dyn_cast_or_null<NamedDecl>(decl.getCanonicalDecl()));
}

struct ModuleReexportSearch {
    const Module                      &target;
    std::unordered_set<const Module *> visited;
};

bool module_reexports(const Module &importer, ModuleReexportSearch &search) {
    if (!search.visited.insert(&importer).second) {
        return false;
    }
    for (const auto &export_decl : importer.Exports) {
#if CLANG_VERSION_MAJOR >= 23
        const Module *exported = export_decl.first;
#else
        const Module *exported = export_decl.getPointer();
#endif
        if (exported == nullptr) {
            continue;
        }
        if (exported == &search.target || exported->getFullModuleName() == search.target.getFullModuleName()) {
            return true;
        }
        if (module_reexports(*exported, search)) {
            return true;
        }
    }
    return false;
}

bool is_reachable_from_importer(const NamedDecl &decl, const NamedDecl &importer, ASTContext &context) {
    const Module *decl_module     = owning_linkage_module(decl);
    const Module *importer_module = owning_linkage_module(importer);
    if (decl_module == nullptr) {
        const SourceLocation location = context.getSourceManager().getExpansionLoc(decl.getLocation());
        return location.isValid() && context.getSourceManager().isInSystemHeader(location);
    }
    if (importer_module == nullptr || !is_importer_reachable_entity(decl)) {
        return false;
    }
    if (decl_module == importer_module || decl_module->getFullModuleName() == importer_module->getFullModuleName()) {
        return true;
    }
    ModuleReexportSearch search{.target = *decl_module};
    return module_reexports(*importer_module, search);
}

std::string stable_entity_identity(const NamedDecl &decl) {
    llvm::SmallString<256> usr;
    if (!clang::index::generateUSRForDecl(&decl, usr)) {
        return fmt::format("usr:{}|module:{}", std::string_view(usr.data(), usr.size()), owning_module_identity(decl));
    }
    return fmt::format("fallback:{}|module:{}", decl.getQualifiedNameAsString(), owning_module_identity(decl));
}

bool ends_with_ci(llvm::StringRef text, llvm::StringRef suffix) {
    if (text.size() < suffix.size()) {
        return false;
    }
    llvm::StringRef tail = text.take_back(suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const auto to_lower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
        if (to_lower(static_cast<unsigned char>(tail[i])) != to_lower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

bool is_header_like_path(llvm::StringRef path) {
    return ends_with_ci(path, ".h") || ends_with_ci(path, ".hh") || ends_with_ci(path, ".hpp") || ends_with_ci(path, ".hxx") ||
           ends_with_ci(path, ".inl") || ends_with_ci(path, ".ipp") || ends_with_ci(path, ".tpp");
}

bool is_gentest_awaitable_return(QualType type) {
    const Type *type_ptr = type.getCanonicalType().getTypePtrOrNull();
    if (!type_ptr) {
        return false;
    }
    const auto *record = type_ptr->getAsCXXRecordDecl();
    const auto *spec   = llvm::dyn_cast_or_null<ClassTemplateSpecializationDecl>(record);
    if (!spec) {
        return false;
    }
    const auto *templ = spec->getSpecializedTemplate();
    return templ && templ->getQualifiedNameAsString() == "gentest::awaitable";
}

std::string mis_scoped_gentest_message(std::string_view attribute) {
    return fmt::format("attribute '{}' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification", attribute);
}

using SuiteAttributeCache = std::unordered_map<const NamespaceDecl *, SuiteAttributeSummary>;

bool should_report_namespace_diag_once(const NamespaceDecl &ns, const SourceManager &sm, std::string_view message) {
    SourceLocation loc = sm.getSpellingLoc(ns.getBeginLoc());
    if (loc.isInvalid()) {
        loc = sm.getSpellingLoc(ns.getLocation());
    }
    const llvm::StringRef file = sm.getFilename(loc);
    const unsigned        line = sm.getSpellingLineNumber(loc);
    std::string           name = ns.getQualifiedNameAsString();
    if (name.empty() && ns.isAnonymousNamespace()) {
        name = "(anonymous namespace)";
    }

    static std::mutex                      mutex;
    static std::unordered_set<std::string> seen;
    const std::string                      key = fmt::format("{}:{}:{}:{}", file.str(), line, name, message);

    std::scoped_lock lock(mutex);
    return seen.insert(key).second;
}

void report_namespace_diag(const NamespaceDecl &ns, const SourceManager &sm, std::string_view message) {
    SourceLocation loc = sm.getSpellingLoc(ns.getBeginLoc());
    if (loc.isInvalid()) {
        loc = sm.getSpellingLoc(ns.getLocation());
    }
    const llvm::StringRef file = sm.getFilename(loc);
    const unsigned        line = sm.getSpellingLineNumber(loc);
    std::string           name = ns.getQualifiedNameAsString();
    if (name.empty() && ns.isAnonymousNamespace()) {
        name = "(anonymous namespace)";
    }

    std::string locpfx;
    if (!file.empty()) {
        locpfx.reserve(file.size() + 24);
        fmt::format_to(std::back_inserter(locpfx), "{}:{}: ", file.str(), line);
    }
    std::string subj;
    if (!name.empty()) {
        subj.reserve(name.size() + 16);
        fmt::format_to(std::back_inserter(subj), " (namespace {})", name);
    }
    log_err("gentest_codegen: {}{}{}\n", locpfx, message, subj);
}

auto find_suite_override(const DeclContext *ctx, const SourceManager &sm, SuiteAttributeCache &suite_cache, bool &had_error)
    -> std::optional<std::string> {
    const DeclContext *current = ctx;
    while (current != nullptr) {
        if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(current)) {
            auto it = suite_cache.find(ns);
            if (it == suite_cache.end()) {
                auto ns_attrs = collect_gentest_attributes_for(*ns, sm);
                for (const auto &msg : ns_attrs.mis_scoped_gentest) {
                    had_error       = true;
                    const auto diag = mis_scoped_gentest_message(msg);
                    if (should_report_namespace_diag_once(*ns, sm, diag)) {
                        report_namespace_diag(*ns, sm, diag);
                    }
                }
                for (const auto &msg : ns_attrs.other_namespaces) {
                    report_namespace_diag(*ns, sm, fmt::format("attribute '{}' ignored (unsupported attribute namespace)", msg));
                }
                auto summary_ns = validate_namespace_attributes(ns_attrs.gentest, [&](const std::string &m) {
                    had_error = true;
                    report_namespace_diag(*ns, sm, m);
                });
                it              = suite_cache.emplace(ns, std::move(summary_ns)).first;
            }
            if (it->second.suite_name.has_value()) {
                return it->second.suite_name;
            }
        }
        current = current->getParent();
    }
    return std::nullopt;
}

[[nodiscard]] std::string derive_namespace_path(const DeclContext *ctx) {
    std::vector<std::string> parts;
    const DeclContext       *current = ctx;
    while (current != nullptr) {
        if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(current)) {
            if (!ns->isAnonymousNamespace()) {
                parts.push_back(ns->getNameAsString());
            }
        }
        current = current->getParent();
    }
    if (parts.empty())
        return std::string{};
    std::ranges::reverse(parts);
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out.push_back('/');
        out += parts[i];
    }
    return out;
}

[[nodiscard]] std::vector<std::string> collect_namespace_parts(const DeclContext *ctx) {
    std::vector<std::string> parts;
    const DeclContext       *current = ctx;
    while (current != nullptr) {
        if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(current)) {
            if (!ns->isAnonymousNamespace()) {
                parts.push_back(ns->getNameAsString());
            }
        }
        current = current->getParent();
    }
    if (parts.empty()) {
        return parts;
    }
    std::ranges::reverse(parts);
    return parts;
}

std::string print_type(QualType type, const PrintingPolicy &policy) {
    std::string              out;
    llvm::raw_string_ostream os(out);
    type.print(os, policy);
    os.flush();
    return out;
}

bool is_std_namespace(const DeclContext *ctx) {
    const auto *current = ctx;
    while (current != nullptr) {
        if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(current)) {
            if (ns->isInlineNamespace()) {
                current = ns->getParent();
                continue;
            }
            return ns->getName() == "std";
        }
        current = current->getParent();
    }
    return false;
}

std::optional<QualType> unwrap_std_shared_ptr(QualType type) {
    QualType spelled_type = type.getUnqualifiedType();
    if (const auto *typedef_type = llvm::dyn_cast<TypedefType>(spelled_type.getTypePtr())) {
        spelled_type = typedef_type->getDecl()->getUnderlyingType().getUnqualifiedType();
    }
    if (const auto *template_type = llvm::dyn_cast<TemplateSpecializationType>(spelled_type.getTypePtr())) {
        const TemplateDecl *template_decl = template_type->getTemplateName().getAsTemplateDecl();
        if (template_decl != nullptr && template_decl->getQualifiedNameAsString() == "std::shared_ptr" &&
            template_type->template_arguments().size() == 1 &&
            template_type->template_arguments().front().getKind() == TemplateArgument::Type) {
            return template_type->template_arguments().front().getAsType();
        }
    }

    type                    = type.getCanonicalType().getUnqualifiedType();
    const auto *record_type = type->getAs<RecordType>();
    if (!record_type) {
        return std::nullopt;
    }
    const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(record_type->getDecl());
    if (!spec) {
        return std::nullopt;
    }
    if (spec->getName() != "shared_ptr") {
        return std::nullopt;
    }
    if (!is_std_namespace(spec->getDeclContext())) {
        return std::nullopt;
    }
    const auto &args = spec->getTemplateArgs();
    if (args.size() != 1 || args[0].getKind() != TemplateArgument::Type) {
        return std::nullopt;
    }
    return args[0].getAsType();
}

QualType infer_fixture_qual_type_from_param(const ParmVarDecl &param) {
    QualType type = param.getType();
    if (type->isReferenceType()) {
        type = type->getPointeeType();
    }
    type = type.getUnqualifiedType();

    if (const auto *pointer = type->getAs<PointerType>()) {
        type = pointer->getPointeeType().getUnqualifiedType();
    }
    if (const auto shared_inner = unwrap_std_shared_ptr(type.getUnqualifiedType())) {
        type = shared_inner->getUnqualifiedType();
    }
    return type.getUnqualifiedType();
}

std::string infer_fixture_type_from_param(const ParmVarDecl &param, const PrintingPolicy &policy) {
    return print_type(infer_fixture_qual_type_from_param(param).getCanonicalType().getUnqualifiedType(), policy);
}

const NamedDecl *adapter_type_declaration(QualType type) {
    type = type.getCanonicalType().getUnqualifiedType();
    if (const auto *record_type = type->getAs<RecordType>(); record_type != nullptr) {
        return record_type->getDecl();
    }
    if (const auto *enum_type = type->getAs<EnumType>(); enum_type != nullptr) {
        return enum_type->getDecl();
    }
    return nullptr;
}

const NamedDecl *adapter_type_naming_declaration(QualType type) {
    type = type.getUnqualifiedType();
    if (const auto *typedef_type = llvm::dyn_cast<TypedefType>(type.getTypePtr())) {
        return typedef_type->getDecl();
    }
    if (const auto *specialization = llvm::dyn_cast<TemplateSpecializationType>(type.getTypePtr())) {
        if (const auto *alias_template =
                llvm::dyn_cast_or_null<TypeAliasTemplateDecl>(specialization->getTemplateName().getAsTemplateDecl())) {
            return alias_template;
        }
    }
#if CLANG_VERSION_MAJOR < 22
    if (const auto *elaborated = llvm::dyn_cast<ElaboratedType>(type.getTypePtr())) {
        return adapter_type_naming_declaration(elaborated->getNamedType());
    }
#endif
    if (const auto *attributed = llvm::dyn_cast<AttributedType>(type.getTypePtr())) {
        return adapter_type_naming_declaration(attributed->getModifiedType());
    }
    if (const auto *paren = llvm::dyn_cast<ParenType>(type.getTypePtr())) {
        return adapter_type_naming_declaration(paren->getInnerType());
    }
    return adapter_type_declaration(type);
}

bool is_importer_reachable_type(QualType type, const FunctionDecl &importer, ASTContext &context) {
    type = type.getUnqualifiedType();
    if (type->isBuiltinType()) {
        return true;
    }
    const NamedDecl *decl = adapter_type_naming_declaration(type);
    if (decl == nullptr) {
        return false;
    }
    return is_reachable_from_importer(*decl, importer, context);
}

struct BindingName {
    bool                     absolute = false;
    std::vector<std::string> parts;
};

bool is_identifier_start(char ch) { return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_'; }

bool is_identifier_continue(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_'; }

bool is_binding_keyword(std::string_view name) {
    static constexpr std::array keywords{
        std::string_view{"alignof"},  std::string_view{"auto"},     std::string_view{"bool"},     std::string_view{"char"},
        std::string_view{"char8_t"},  std::string_view{"char16_t"}, std::string_view{"char32_t"}, std::string_view{"const"},
        std::string_view{"decltype"}, std::string_view{"double"},   std::string_view{"false"},    std::string_view{"float"},
        std::string_view{"int"},      std::string_view{"long"},     std::string_view{"noexcept"}, std::string_view{"nullptr"},
        std::string_view{"short"},    std::string_view{"signed"},   std::string_view{"sizeof"},   std::string_view{"true"},
        std::string_view{"unsigned"}, std::string_view{"void"},     std::string_view{"volatile"}, std::string_view{"wchar_t"}};
    return std::ranges::find(keywords, name) != keywords.end();
}

std::vector<BindingName> binding_names(std::string_view text) {
    std::vector<BindingName> names;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\'' || text[i] == '"') {
            const char quote = text[i++];
            while (i < text.size()) {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    i += 2;
                    continue;
                }
                if (text[i++] == quote) {
                    break;
                }
            }
            continue;
        }

        BindingName name{};
        if (i + 1 < text.size() && text[i] == ':' && text[i + 1] == ':') {
            name.absolute = true;
            i += 2;
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
                ++i;
            }
        }
        if (i >= text.size() || !is_identifier_start(text[i])) {
            ++i;
            continue;
        }

        for (;;) {
            const std::size_t start = i++;
            while (i < text.size() && is_identifier_continue(text[i])) {
                ++i;
            }
            name.parts.emplace_back(text.substr(start, i - start));

            std::size_t next = i;
            while (next < text.size() && std::isspace(static_cast<unsigned char>(text[next])) != 0) {
                ++next;
            }
            if (next + 1 >= text.size() || text[next] != ':' || text[next + 1] != ':') {
                break;
            }
            next += 2;
            while (next < text.size() && std::isspace(static_cast<unsigned char>(text[next])) != 0) {
                ++next;
            }
            if (next >= text.size() || !is_identifier_start(text[next])) {
                break;
            }
            i = next;
        }

        if (!(name.parts.size() == 1 && is_binding_keyword(name.parts.front()))) {
            names.push_back(std::move(name));
        }
    }
    return names;
}

std::vector<const NamedDecl *> lookup_binding_name(const BindingName &name, const DeclContext &origin, ASTContext &context) {
    if (name.parts.empty()) {
        return {};
    }

    const auto lookup = [&](const DeclContext &decl_context, std::string_view part) {
        std::vector<const NamedDecl *> matches;
        const DeclarationName          declaration_name(&context.Idents.get(part));
        for (const auto *decl : decl_context.lookup(declaration_name)) {
            if (const auto *named = llvm::dyn_cast<NamedDecl>(decl)) {
                matches.push_back(named);
            }
        }
        return matches;
    };
    const auto as_context = [](const NamedDecl &decl) -> const DeclContext * {
        if (const auto *alias = llvm::dyn_cast<NamespaceAliasDecl>(&decl)) {
            return alias->getNamespace();
        }
        if (const auto *decl_context = llvm::dyn_cast<DeclContext>(&decl)) {
            return decl_context;
        }
        if (const auto *typedef_name = llvm::dyn_cast<TypedefNameDecl>(&decl)) {
            const QualType underlying = typedef_name->getUnderlyingType().getCanonicalType();
            if (const auto *record = underlying->getAs<RecordType>()) {
                return record->getDecl();
            }
            if (const auto *enumeration = underlying->getAs<EnumType>()) {
                return enumeration->getDecl();
            }
        }
        return nullptr;
    };

    std::vector<const NamedDecl *> current;
    if (name.absolute) {
        current = lookup(*context.getTranslationUnitDecl(), name.parts.front());
    } else {
        for (const DeclContext *scope = &origin; scope != nullptr; scope = scope->getParent()) {
            current = lookup(*scope, name.parts.front());
            if (!current.empty()) {
                break;
            }
        }
    }
    for (std::size_t part_idx = 1; part_idx < name.parts.size() && !current.empty(); ++part_idx) {
        std::vector<const NamedDecl *> next;
        for (const auto *candidate : current) {
            if (const DeclContext *scope = as_context(*candidate)) {
                auto matches = lookup(*scope, name.parts[part_idx]);
                next.insert(next.end(), matches.begin(), matches.end());
            }
        }
        current = std::move(next);
    }
    return current;
}

bool validate_importer_binding(const FunctionDecl &function, std::string_view binding, std::string_view binding_kind, ASTContext &context,
                               const std::function<void(const std::string &)> &report) {
    for (const auto &name : binding_names(binding)) {
        const auto declarations = lookup_binding_name(name, *function.getDeclContext(), context);
        if (declarations.empty()) {
            continue;
        }
        const auto reachable = [&](const NamedDecl *decl) {
            return decl != nullptr && is_reachable_from_importer(*decl, function, context);
        };
        if (std::ranges::any_of(declarations, reachable)) {
            continue;
        }
        const auto *decl = declarations.front();
        report(fmt::format("{} '{}' names '{}' which is not reachable after importing named module '{}'", binding_kind, binding,
                           decl->getQualifiedNameAsString(), owning_module_identity(function)));
        return false;
    }
    return true;
}

bool validate_importer_template_bindings(const FunctionDecl &function, const std::vector<std::vector<std::string>> &combinations,
                                         ASTContext &context, const std::function<void(const std::string &)> &report) {
    for (const auto &combination : combinations) {
        for (const auto &binding : combination) {
            if (!validate_importer_binding(function, binding, "template binding", context, report)) {
                return false;
            }
        }
    }
    return true;
}

bool is_gentest_mock_adapter_type(QualType type) {
    const auto *record = llvm::dyn_cast_or_null<ClassTemplateSpecializationDecl>(adapter_type_declaration(type));
    return record != nullptr && record->getSpecializedTemplate()->getQualifiedNameAsString() == "gentest::mock";
}

// Numeric axis generators (range/linspace/geom/logspace) are expanded while
// generating code, so their bounds must be numeric literals. Parse the whole
// literal or nothing: a partially parsed bound silently changes the cases.
enum class AxisNumberKind { Integer, Floating, Unsupported };

AxisNumberKind classify_axis_type(QualType type, const ASTContext &context) {
    type = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
    if (type.isNull() || type->isDependentType()) {
        return AxisNumberKind::Unsupported;
    }
    if (type->isIntegralType(context)) {
        return AxisNumberKind::Integer;
    }
    if (type->isRealFloatingType()) {
        return AxisNumberKind::Floating;
    }
    return AxisNumberKind::Unsupported;
}

std::string_view trim_axis_text(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

// Splits an optional sign from a literal and drops C++14 digit separators.
// Returns false when a separator is not between two digits.
bool split_axis_literal(std::string_view text, bool &negative, std::string &digits) {
    text     = trim_axis_text(text);
    negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
        text = trim_axis_text(text);
    }
    digits.clear();
    digits.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\'') {
            digits.push_back(text[i]);
            continue;
        }
        if (i == 0 || i + 1 >= text.size() || std::isxdigit(static_cast<unsigned char>(text[i - 1])) == 0 ||
            std::isxdigit(static_cast<unsigned char>(text[i + 1])) == 0) {
            return false;
        }
    }
    return !digits.empty();
}

// Decimal, hexadecimal, binary and octal integer literals with digit
// separators and u/l/ll/z suffixes, when the value fits in long long.
std::optional<long long> parse_axis_integer_literal(std::string_view text) {
    bool        negative = false;
    std::string digits;
    if (!split_axis_literal(text, negative, digits)) {
        return std::nullopt;
    }
    std::string_view body = digits;
    int              base = 10;
    if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) {
        base = 16;
        body.remove_prefix(2);
    } else if (body.size() > 2 && body[0] == '0' && (body[1] == 'b' || body[1] == 'B')) {
        base = 2;
        body.remove_prefix(2);
    } else if (body.size() > 1 && body[0] == '0') {
        base = 8;
        body.remove_prefix(1);
    }
    std::size_t suffix_length = 0;
    while (suffix_length < body.size() && suffix_length < 3) {
        const char ch = body[body.size() - suffix_length - 1];
        if (ch != 'u' && ch != 'U' && ch != 'l' && ch != 'L' && ch != 'z' && ch != 'Z') {
            break;
        }
        ++suffix_length;
    }
    body.remove_suffix(suffix_length);
    if (body.empty()) {
        return std::nullopt;
    }
    unsigned long long magnitude = 0;
    const auto [end, error]      = std::from_chars(body.data(), body.data() + body.size(), magnitude, base);
    if (error != std::errc{} || end != body.data() + body.size()) {
        return std::nullopt;
    }
    constexpr auto max_positive = static_cast<unsigned long long>(std::numeric_limits<long long>::max());
    if (negative) {
        if (magnitude > max_positive + 1ULL) {
            return std::nullopt;
        }
        return magnitude == max_positive + 1ULL ? std::numeric_limits<long long>::min() : -static_cast<long long>(magnitude);
    }
    if (magnitude > max_positive) {
        return std::nullopt;
    }
    return static_cast<long long>(magnitude);
}

// Integer literals plus decimal/hexadecimal floating literals with an optional
// exponent and f/l suffix. Names, expressions, inf and nan are rejected.
std::optional<double> parse_axis_floating_literal(std::string_view text) {
    if (const auto integer = parse_axis_integer_literal(text); integer.has_value()) {
        return static_cast<double>(*integer);
    }
    bool        negative = false;
    std::string digits;
    if (!split_axis_literal(text, negative, digits)) {
        return std::nullopt;
    }
    if (std::isdigit(static_cast<unsigned char>(digits.front())) == 0 && digits.front() != '.') {
        return std::nullopt;
    }
    const bool hexadecimal  = digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X');
    const bool has_exponent = digits.find_first_of(hexadecimal ? "pP" : "eE") != std::string::npos;
    const char last         = digits.back();
    if ((last == 'f' || last == 'F' || last == 'l' || last == 'L') && (!hexadecimal || has_exponent)) {
        digits.pop_back();
    }
    if (digits.empty() || (hexadecimal && !has_exponent)) {
        return std::nullopt;
    }
    char        *end   = nullptr;
    const double value = std::strtod(digits.c_str(), &end);
    if (end != digits.c_str() + digits.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return negative ? -value : value;
}

// An integer bound may be written as a floating literal with an integral
// value, for example a count of 1e3.
std::optional<long long> parse_axis_integer(std::string_view text) {
    if (const auto integer = parse_axis_integer_literal(text); integer.has_value()) {
        return integer;
    }
    const auto floating = parse_axis_floating_literal(text);
    if (!floating.has_value() || std::trunc(*floating) != *floating || std::abs(*floating) >= 9.2e18) {
        return std::nullopt;
    }
    return static_cast<long long>(*floating);
}

// Upper bound for one generated axis; anything larger is a mistyped bound.
constexpr std::size_t kMaxAxisValues = 1'000'000;

struct FunctionParamInfo {
    const ParmVarDecl          *decl = nullptr;
    std::string                 name;
    std::string                 type_spelling;
    bool                        is_value_axis   = false;
    bool                        has_default_arg = false;
    bool                        is_fixture      = false;
    std::size_t                 fixture_index   = 0;
    std::optional<FixtureScope> required_scope;
    // Position in a template argument combination when the fixture type is
    // exactly a bound template type parameter (template <typename T> f(T &)).
    std::optional<std::size_t> template_binding_index;
};

// A template binding resolved to the fixture type it names.
struct BoundFixtureType {
    std::string      type_name;
    const NamedDecl *declaration = nullptr;
};

bool is_simple_type_name(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char ch) {
        return is_identifier_continue(ch) || ch == ':' || std::isspace(static_cast<unsigned char>(ch)) != 0;
    });
}

// Resolves a template binding such as "FxA" or "ns::FxA", written in the
// scope of `function`, to the fully qualified type it names. Fundamental type
// names are returned unchanged. Anything that is not a plain (qualified) name
// of a class, enum or alias yields nullopt.
std::optional<BoundFixtureType> resolve_bound_fixture_type(std::string_view binding, const FunctionDecl &function, ASTContext &context,
                                                           const PrintingPolicy &policy) {
    while (!binding.empty() && std::isspace(static_cast<unsigned char>(binding.front())) != 0) {
        binding.remove_prefix(1);
    }
    while (!binding.empty() && std::isspace(static_cast<unsigned char>(binding.back())) != 0) {
        binding.remove_suffix(1);
    }
    if (!is_simple_type_name(binding)) {
        return std::nullopt;
    }
    const auto names = binding_names(binding);
    if (names.empty()) {
        // Only keywords: a fundamental type such as "int" or "unsigned long".
        return BoundFixtureType{.type_name = std::string(binding)};
    }
    if (names.size() != 1) {
        return std::nullopt;
    }
    std::optional<BoundFixtureType> resolved;
    for (const NamedDecl *candidate : lookup_binding_name(names.front(), *function.getDeclContext(), context)) {
        BoundFixtureType bound;
        if (const auto *alias = llvm::dyn_cast<TypedefNameDecl>(candidate)) {
            const QualType underlying = alias->getUnderlyingType().getCanonicalType().getUnqualifiedType();
            if (underlying.isNull() || underlying->isDependentType()) {
                return std::nullopt;
            }
            bound.type_name   = print_type(underlying, policy);
            bound.declaration = adapter_type_declaration(underlying);
        } else if (const auto *tag = llvm::dyn_cast<TagDecl>(candidate)) {
            if (const auto *record = llvm::dyn_cast<CXXRecordDecl>(tag);
                record != nullptr && record->getDescribedClassTemplate() != nullptr) {
                return std::nullopt;
            }
            bound.type_name   = tag->getQualifiedNameAsString();
            bound.declaration = tag->getCanonicalDecl();
        } else {
            continue;
        }
        if (resolved.has_value() && resolved->type_name != bound.type_name) {
            return std::nullopt;
        }
        resolved = std::move(bound);
    }
    return resolved;
}
} // namespace

TestCaseCollector::TestCaseCollector(std::vector<TestCaseInfo> &out, bool strict_fixture, bool allow_includes,
                                     bool header_declaration_registration, bool module_importer_registration)
    : out_(out), strict_fixture_(strict_fixture), allow_includes_(allow_includes),
      header_declaration_registration_(header_declaration_registration), module_importer_registration_(module_importer_registration) {}

void TestCaseCollector::run(const MatchFinder::MatchResult &result) {
    const auto *func = result.Nodes.getNodeAs<FunctionDecl>("gentest.func");
    if (func == nullptr) {
        return;
    }
    if (!header_declaration_registration_ && !module_importer_registration_ && !func->isThisDeclarationADefinition()) {
        return;
    }

    const auto *sm = result.SourceManager;

    // Allow templated functions; instantiation handled by codegen.

    auto loc = func->getBeginLoc();
    if (loc.isInvalid()) {
        return;
    }
    if (loc.isMacroID()) {
        loc = sm->getExpansionLoc(loc);
    }

    const bool in_main_file = sm->isWrittenInMainFile(loc);
    if (!in_main_file) {
        if (!allow_includes_) {
            return;
        }
    }

    if (sm->isInSystemHeader(loc) || sm->isWrittenInBuiltinFile(loc)) {
        return;
    }

    // Inline classification to support template/parameter expansion
    const auto  collected = collect_gentest_attributes_for(*func, *sm);
    const auto &parsed    = collected.gentest;
    auto        report    = [&](std::string_view message) {
        const SourceLocation  sloc    = sm->getSpellingLoc(func->getBeginLoc());
        const llvm::StringRef file    = sm->getFilename(sloc);
        const unsigned        lnum    = sm->getSpellingLineNumber(sloc);
        const std::string     subject = func->getQualifiedNameAsString();
        std::string           locpfx;
        if (!file.empty()) {
            locpfx.reserve(file.size() + 24);
            fmt::format_to(std::back_inserter(locpfx), "{}:{}: ", file.str(), lnum);
        }
        std::string subj;
        if (!subject.empty()) {
            subj.reserve(subject.size() + 4);
            fmt::format_to(std::back_inserter(subj), " ({})", subject);
        }
        log_err("gentest_codegen: {}{}{}\n", locpfx, message, subj);
    };
    for (const auto &message : collected.mis_scoped_gentest) {
        had_error_ = true;
        report(mis_scoped_gentest_message(message));
    }
    for (const auto &message : collected.other_namespaces) {
        report(fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
    }
    if (parsed.empty())
        return;

    if (header_declaration_registration_) {
        const auto main_file = sm->getFilename(sm->getFileLoc(loc));
        if (in_main_file && !is_header_like_path(main_file)) {
            had_error_ = true;
            report("Gentest attributes in an authored .cpp are not supported by header-declaration registration; annotate the header "
                   "declaration instead");
            return;
        }
        if (!func->hasExternalFormalLinkage()) {
            had_error_ = true;
            report("header-declared Gentest cases require external linkage; static and anonymous-namespace cases are not supported");
            return;
        }
        if (func->isThisDeclarationADefinition() && !func->isInlined() && func->getDescribedFunctionTemplate() == nullptr) {
            had_error_ = true;
            report("a Gentest case definition in a header must be inline; put a declaration in the header and the definition in a normally "
                   "compiled .cpp");
            return;
        }
    }
    auto summary = validate_attributes(parsed, [&](const std::string &m) {
        had_error_ = true;
        report(m);
    });
    if (!summary.is_case)
        return;
    if (func->isDeleted())
        return;
    const bool importer_reachable = is_importer_reachable_entity(*func);
    if (module_importer_registration_ && !importer_reachable) {
        had_error_ = true;
        report(fmt::format("Gentest case is not reachable after importing named module '{}'; MODULE_REGISTRATION uses an ordinary "
                           "importer translation unit, so export the declaration",
                           owning_module_identity(*func)));
        return;
    }

    std::string qualified = func->getQualifiedNameAsString();
    if (qualified.empty())
        qualified = func->getNameAsString();
    if (qualified.find("(anonymous namespace)") != std::string::npos) {
        log_err("gentest_codegen: ignoring test in anonymous namespace: {}\n", qualified);
        return;
    }
    auto file_loc = sm->getFileLoc(func->getLocation());
    auto filename = sm->getFilename(file_loc);
    if (filename.empty())
        return;
    unsigned lnum = sm->getSpellingLineNumber(file_loc);

    // Validate template attribute usage and collect declaration order (optional under flag).
    std::vector<TemplateParamInfo> fn_params_order;
    if (!summary.template_sets.empty()) {
        if (!disc::collect_template_params(*func, fn_params_order)) {
#ifndef GENTEST_DISABLE_TEMPLATE_VALIDATION
            had_error_ = true;
            report("'template(...)' attributes present but function is not a template");
            return;
#else
            fn_params_order.clear(); // fallback to attribute order later
#endif
        }
#ifndef GENTEST_DISABLE_TEMPLATE_VALIDATION
        if (!fn_params_order.empty()) {
            if (!disc::validate_template_attributes(summary.template_sets, fn_params_order, [&](const std::string &m) {
                    had_error_ = true;
                    report(m);
                })) {
                return;
            }
        }
#endif
    }

    const FunctionTemplateDecl *function_template = func->getDescribedFunctionTemplate();
    if (summary.template_sets.empty() && function_template != nullptr) {
        // Without template(...) the case is emitted as name<>(...), which only
        // compiles when every parameter is defaulted, a pack, or deduced from
        // the generated call arguments.
        llvm::SmallBitVector deduced;
        Sema::MarkDeducedTemplateParameters(*result.Context, function_template, deduced);
        const TemplateParameterList &template_params = *function_template->getTemplateParameters();
        for (unsigned index = 0; index < template_params.size(); ++index) {
            const NamedDecl *template_param = template_params.getParam(index);
            bool             has_default    = false;
            if (const auto *type_param = llvm::dyn_cast<TemplateTypeParmDecl>(template_param)) {
                has_default = type_param->hasDefaultArgument();
            } else if (const auto *value_param = llvm::dyn_cast<NonTypeTemplateParmDecl>(template_param)) {
                has_default = value_param->hasDefaultArgument();
            } else if (const auto *template_template_param = llvm::dyn_cast<TemplateTemplateParmDecl>(template_param)) {
                has_default = template_template_param->hasDefaultArgument();
            }
            const bool is_deduced = index < deduced.size() && deduced.test(index);
            if (has_default || template_param->isParameterPack() || is_deduced) {
                continue;
            }
            had_error_ = true;
            report(fmt::format("function template requires a 'template({0}, ...)' attribute: template parameter '{0}' has no default "
                               "argument and cannot be deduced from the generated call",
                               template_param->getNameAsString()));
            return;
        }
    }

    // Build combined template argument combinations
    std::vector<std::vector<std::string>> combined_tpl_combos;
    if (!summary.template_sets.empty()) {
#ifndef GENTEST_DISABLE_TEMPLATE_VALIDATION
        if (!fn_params_order.empty()) {
            combined_tpl_combos = disc::build_template_arg_combos(summary.template_sets, fn_params_order);
        } else
#endif
        {
            combined_tpl_combos = disc::build_template_arg_combos_attr_order(summary.template_sets);
        }
    }
    if (combined_tpl_combos.empty())
        combined_tpl_combos.emplace_back();
    if (module_importer_registration_ &&
        !validate_importer_template_bindings(*func, combined_tpl_combos, *result.Context, [&](const std::string &message) {
            had_error_ = true;
            report(message);
        })) {
        return;
    }

    const bool is_function_template = func->getDescribedFunctionTemplate() != nullptr;
    auto       make_qualified       = [&](const std::vector<std::string> &tpl_ordered) {
        if (!is_function_template)
            return qualified;
        std::string q = qualified;
        q += '<';
        for (std::size_t i = 0; i < tpl_ordered.size(); ++i) {
            if (i)
                q += ", ";
            q += tpl_ordered[i];
        }
        q += '>';
        return q;
    };
    auto make_display = [&](const std::string &base, const std::vector<std::string> &tpl_ordered, const std::string &call_args) {
        std::string nm = base;
        if (is_function_template) {
            nm += '<';
            for (std::size_t i = 0; i < tpl_ordered.size(); ++i) {
                if (i)
                    nm += ',';
                nm += tpl_ordered[i];
            }
            nm += '>';
        }
        if (!call_args.empty()) {
            nm += '(';
            nm += call_args;
            nm += ')';
        }
        return nm;
    };
    const std::vector<std::string> namespace_parts = collect_namespace_parts(func->getDeclContext());

    const auto  suite_override = find_suite_override(func->getDeclContext(), *sm, suite_cache_, had_error_);
    std::string suite_path     = suite_override.value_or(derive_namespace_path(func->getDeclContext()));
    std::string base_case_name;
    if (summary.case_name.has_value()) {
        base_case_name = *summary.case_name;
    } else if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(func)) {
        const auto       *fixture      = method->getParent();
        const std::string fixture_name = fixture ? fixture->getNameAsString() : std::string{};
        const std::string method_name  = method->getNameAsString();
        if (!fixture_name.empty()) {
            base_case_name = fixture_name + "/" + method_name;
        } else {
            base_case_name = method_name;
        }
    } else {
        base_case_name = func->getNameAsString();
    }
    std::string final_base = suite_path.empty() ? base_case_name : (suite_path + "/" + base_case_name);
    // Header-declaration mode sees the same source tokens through more than
    // one TU. Defer uniqueness to the deterministic cross-slot merge.
    if (!header_declaration_registration_) {
        const SourceLocation  sloc    = sm->getSpellingLoc(func->getBeginLoc());
        const llvm::StringRef file    = sm->getFilename(sloc);
        const unsigned        lnum    = sm->getSpellingLineNumber(sloc);
        const std::string     here    = fmt::format("{}:{}", file.str(), lnum);
        auto                  it_base = unique_base_locations_.find(final_base);
        if (it_base == unique_base_locations_.end()) {
            unique_base_locations_.emplace(final_base, here);
        } else {
            had_error_ = true;
            log_err("gentest_codegen: duplicate test name '{}' at {} (previously declared at {})\n", final_base, here, it_base->second);
            return; // do not emit duplicates
        }
    }

    const std::string declaration_site_key = declaration_site_identity(*func, *sm, "function");
    const std::string entity_key           = stable_entity_identity(*func);
    const std::string canonical_signature  = func->getType().getCanonicalType().getAsString();

    std::string tu_filename;
    {
        const SourceLocation  tu_loc  = sm->getLocForStartOfFile(sm->getMainFileID());
        const llvm::StringRef tu_file = sm->getFilename(tu_loc);
        tu_filename                   = tu_file.str();
    }

    struct FixtureContext {
        std::string     qualified_name;
        FixtureLifetime lifetime = FixtureLifetime::MemberEphemeral;
    };
    std::optional<FixtureContext> fixture_ctx;
    if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(func)) {
        if (const auto *record = method->getParent()) {
            const auto class_attrs = collect_gentest_attributes_for(*record, *sm);
            for (const auto &message : class_attrs.mis_scoped_gentest) {
                had_error_ = true;
                report(mis_scoped_gentest_message(message));
            }
            for (const auto &message : class_attrs.other_namespaces)
                report(fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
            auto           fixture_summary = validate_fixture_attributes(class_attrs.gentest, [&](const std::string &m) {
                had_error_ = true;
                report(m);
            });
            FixtureContext ctx;
            ctx.qualified_name = record->getQualifiedNameAsString();
            if (fixture_summary.lifetime == FixtureLifetime::MemberSuite && suite_path.empty()) {
                had_error_ = true;
                report("'fixture(suite)' requires an enclosing named namespace to derive a suite path");
                ctx.lifetime = FixtureLifetime::MemberEphemeral;
            } else {
                ctx.lifetime = fixture_summary.lifetime;
            }
            if (ctx.lifetime == FixtureLifetime::MemberSuite || ctx.lifetime == FixtureLifetime::MemberGlobal) {
                if (strict_fixture_) {
                    had_error_ = true;
                    if (ctx.lifetime == FixtureLifetime::MemberSuite) {
                        report("suite fixtures cannot declare member tests; move assertions to setUp()/tearDown() or use an ephemeral "
                               "fixture");
                    } else {
                        report("global fixtures cannot declare member tests; move assertions to setUp()/tearDown() or use an ephemeral "
                               "fixture");
                    }
                }
            }
            fixture_ctx = std::move(ctx);
        }
    }

    const bool returns_async = is_gentest_awaitable_return(func->getReturnType());
    if (returns_async && (summary.is_benchmark || summary.is_jitter)) {
        had_error_ = true;
        report("gentest::awaitable<T> return types are supported only for test cases, not bench or jitter cases");
        return;
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    auto add_case = [&](const std::vector<std::string> &tpl_ordered, const std::string &display_args, const std::string &call_args,
                        const std::vector<std::string> &free_fixture_types, const std::vector<std::string> &free_fixture_emit_types,
                        const std::vector<std::string>                 &free_fixture_entity_keys,
                        const std::vector<std::optional<FixtureScope>> &free_fixture_required_scopes,
                        const std::vector<FreeCallArg>                 &free_call_args) {
        TestCaseInfo info{};
        info.qualified_name               = make_qualified(tpl_ordered);
        info.display_name                 = make_display(final_base, tpl_ordered, display_args);
        info.base_name                    = final_base;
        info.tu_filename                  = tu_filename;
        info.filename                     = filename.str();
        info.suite_name                   = suite_path;
        info.line                         = lnum;
        info.declaration_site_key         = declaration_site_key;
        info.entity_key                   = entity_key;
        info.tags                         = summary.tags;
        info.requirements                 = summary.requirements;
        info.owner                        = summary.owner.value_or(std::string{});
        info.should_skip                  = summary.should_skip;
        info.skip_reason                  = summary.skip_reason;
        info.is_benchmark                 = summary.is_benchmark;
        info.is_jitter                    = summary.is_jitter;
        info.is_baseline                  = summary.is_baseline;
        info.items_per_call               = summary.items_per_call;
        info.benchmark                    = summary.benchmark;
        info.template_args                = tpl_ordered;
        info.call_arguments               = call_args;
        info.is_function_template         = is_function_template;
        info.returns_value                = !func->getReturnType()->isVoidType();
        info.returns_async                = returns_async;
        info.namespace_parts              = namespace_parts;
        info.free_fixture_types           = free_fixture_types;
        info.free_fixture_entity_keys     = free_fixture_entity_keys;
        info.free_fixture_emit_types      = free_fixture_emit_types;
        info.free_fixture_required_scopes = free_fixture_required_scopes;
        info.free_call_args               = free_call_args;
        if (fixture_ctx) {
            info.fixture_qualified_name = fixture_ctx->qualified_name;
            info.fixture_lifetime       = fixture_ctx->lifetime;
        }
        info.semantic_fingerprint =
            fmt::format("signature:{}|linkage:{}|module:{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", canonical_signature,
                        static_cast<int>(func->getFormalLinkage()), owning_module_identity(*func), info.qualified_name, info.display_name,
                        info.base_name, info.suite_name, info.call_arguments, info.fixture_qualified_name,
                        static_cast<int>(info.fixture_lifetime), info.is_benchmark, info.is_jitter, info.is_baseline, info.returns_value,
                        info.returns_async, info.items_per_call, info.should_skip, info.skip_reason, info.owner);
        info.semantic_fingerprint +=
            fmt::format("|bench-once:{}|bench-supplied:{}|bench-warmup:{}", info.benchmark.once, info.benchmark.supplied_time,
                        info.benchmark.warmup_epochs ? std::to_string(*info.benchmark.warmup_epochs) : "default");
        for (const auto &tag : info.tags) {
            info.semantic_fingerprint += "|tag:" + tag;
        }
        for (const auto &requirement : info.requirements) {
            info.semantic_fingerprint += "|requirement:" + requirement;
        }
        for (const auto &template_arg : info.template_args) {
            info.semantic_fingerprint += "|template:" + template_arg;
        }
        for (const auto &fixture_type : info.free_fixture_types) {
            info.semantic_fingerprint += "|fixture:" + fixture_type;
        }
        for (const auto &fixture_type : info.free_fixture_emit_types) {
            info.semantic_fingerprint += "|fixture-emit:" + fixture_type;
        }
        for (const auto &fixture_entity : info.free_fixture_entity_keys) {
            info.semantic_fingerprint += "|fixture-entity:" + fixture_entity;
        }
        for (const auto required_scope : info.free_fixture_required_scopes) {
            info.semantic_fingerprint +=
                required_scope.has_value() ? fmt::format("|fixture-scope:{}", static_cast<int>(*required_scope)) : "|fixture-scope:local";
        }
        std::string key = info.qualified_name + "#" + info.display_name + "@" + info.filename + ":" + std::to_string(info.line);
        if (seen_.insert(key).second)
            out_.push_back(std::move(info));
    };

    auto policy = PrintingPolicy(result.Context->getLangOpts());
    policy.adjustForCPlusPlus();
    policy.SuppressScope          = false;
    policy.FullyQualifiedName     = true;
    policy.SuppressUnwrittenScope = false;

    std::vector<FunctionParamInfo> function_params;
    function_params.reserve(func->parameters().size());
    std::map<std::string, std::string> param_types;
    for (const ParmVarDecl *p : func->parameters()) {
        FunctionParamInfo param{};
        param.decl            = p;
        param.name            = p->getNameAsString();
        param.type_spelling   = print_type(p->getType(), policy);
        param.has_default_arg = p->hasDefaultArg();
        if (!param.name.empty()) {
            param_types.emplace(param.name, param.type_spelling);
        }
        function_params.push_back(std::move(param));
    }

    // Validate: all named axes refer to known parameters; no overlaps.
    std::set<std::string> value_axis_names;
    auto                  validate_axis_name = [&](const std::string &name, std::string_view origin) -> bool {
        if (!param_types.contains(name)) {
            had_error_ = true;
            report(fmt::format("unknown parameter name '{}' in {}(...)", name, origin));
            return false;
        }
        if (!value_axis_names.insert(name).second) {
            had_error_ = true;
            report(fmt::format("duplicate parameter axis for '{}'", name));
            return false;
        }
        return true;
    };

    for (const auto &ps : summary.parameter_sets) {
        if (!validate_axis_name(ps.param_name, "parameters")) {
            return;
        }
    }
    for (const auto &rs : summary.parameter_ranges) {
        if (!validate_axis_name(rs.name, "parameters_range")) {
            return;
        }
    }
    for (const auto &ls : summary.parameter_linspaces) {
        if (!validate_axis_name(ls.name, "parameters_linspace")) {
            return;
        }
    }
    for (const auto &gs : summary.parameter_geoms) {
        if (!validate_axis_name(gs.name, "parameters_geom")) {
            return;
        }
    }
    for (const auto &ls : summary.parameter_logspaces) {
        if (!validate_axis_name(ls.name, "logspace")) {
            return;
        }
    }
    for (const auto &pp : summary.param_packs) {
        for (const auto &nm : pp.names) {
            if (!param_types.contains(nm)) {
                had_error_ = true;
                report(fmt::format("unknown parameter name '{}' in parameters_pack(...)", nm));
                return;
            }
            if (!value_axis_names.insert(nm).second) {
                had_error_ = true;
                report(fmt::format("parameter '{}' supplied by multiple parameter sets/packs", nm));
                return;
            }
        }
    }

    std::vector<std::string>                 inferred_fixture_types;
    std::vector<std::string>                 inferred_fixture_entity_keys;
    std::vector<std::string>                 inferred_fixture_emit_types;
    std::vector<std::optional<FixtureScope>> inferred_fixture_required_scopes;
    for (auto &param : function_params) {
        if (!param.name.empty() && value_axis_names.contains(param.name)) {
            param.is_value_axis = true;
            continue;
        }
        if (param.has_default_arg) {
            continue;
        }
        param.is_fixture    = true;
        param.fixture_index = inferred_fixture_types.size();
        if (!param.decl) {
            had_error_ = true;
            report("internal error while inferring fixture parameter type");
            return;
        }
        const QualType fixture_qual_type = infer_fixture_qual_type_from_param(*param.decl);
        std::string    fixture_type      = infer_fixture_type_from_param(*param.decl, policy);
        std::string    fixture_emit_type = fixture_type;
        if (fixture_qual_type->isDependentType()) {
            // The printed type would be "type-parameter-0-0". Substitute the
            // bound template argument per combination when the fixture type
            // is exactly a template type parameter; reject everything else.
            const std::string param_name       = param.name.empty() ? std::string("<unnamed>") : param.name;
            const auto       *template_type    = fixture_qual_type.getCanonicalType()->getAs<TemplateTypeParmType>();
            const auto       *template_params  = function_template != nullptr ? function_template->getTemplateParameters() : nullptr;
            bool              binding_resolved = false;
            if (template_type != nullptr && template_params != nullptr && template_type->getDepth() == 0 &&
                template_type->getIndex() < template_params->size() && !template_type->isParameterPack() &&
                !summary.template_sets.empty()) {
                binding_resolved = true;
                for (unsigned index = 0; index < template_type->getIndex(); ++index) {
                    binding_resolved = binding_resolved && !template_params->getParam(index)->isParameterPack();
                }
            }
            if (!binding_resolved) {
                had_error_ = true;
                report(fmt::format("fixture parameter '{}' has type '{}', which depends on a template parameter; a fixture parameter type "
                                   "may only be a template type parameter bound by 'template(...)' (not a pack and not after a pack), "
                                   "or a non-dependent type",
                                   param_name, param.type_spelling));
                return;
            }
            param.template_binding_index = template_type->getIndex();
            inferred_fixture_types.push_back(std::move(fixture_type));
            inferred_fixture_entity_keys.emplace_back();
            inferred_fixture_emit_types.push_back(std::move(fixture_emit_type));
            inferred_fixture_required_scopes.push_back(param.required_scope);
            continue;
        }
        if (fixture_type.empty()) {
            had_error_                      = true;
            const std::string fallback_name = param.name.empty() ? std::string("<unnamed>") : param.name;
            report(fmt::format("unable to infer fixture type for parameter '{}'", fallback_name));
            return;
        }
        if (module_importer_registration_) {
            PrintingPolicy emit_policy     = policy;
            emit_policy.SuppressScope      = false;
            emit_policy.FullyQualifiedName = true;
            // The generic type printer preserves source spelling and can omit namespace qualifiers. Generated importer code needs a
            // stable name valid outside the declaration's original scope.
            fixture_emit_type = TypeName::getFullyQualifiedName(fixture_qual_type, *result.Context, emit_policy);
            if (is_gentest_mock_adapter_type(fixture_qual_type)) {
                had_error_ = true;
                report(fmt::format("MODULE_REGISTRATION cannot generate direct mocks owned by named module '{}'; export a mock-provider "
                                   "API from the module and attach its generated implementation explicitly",
                                   owning_module_identity(*func)));
                return;
            }
            if (!is_importer_reachable_type(fixture_qual_type, *func, *result.Context)) {
                had_error_ = true;
                report(fmt::format("fixture type '{}' is not reachable after importing named module '{}'; the generated registration "
                                   "importer must name and instantiate this type",
                                   fixture_type, owning_module_identity(*func)));
                return;
            }
        }
        inferred_fixture_types.push_back(std::move(fixture_type));
        if (const NamedDecl *fixture_decl = adapter_type_declaration(fixture_qual_type); fixture_decl != nullptr) {
            inferred_fixture_entity_keys.push_back(stable_entity_identity(*fixture_decl));
        } else {
            inferred_fixture_entity_keys.emplace_back();
        }
        inferred_fixture_emit_types.push_back(std::move(fixture_emit_type));
        inferred_fixture_required_scopes.push_back(param.required_scope);
    }

    // Cartesian product of scalar parameter axes across names.
    std::vector<std::vector<std::pair<std::string, std::string>>> scalar_axes; // vector of (name,value) arrays
    for (const auto &ps : summary.parameter_sets) {
        std::vector<std::pair<std::string, std::string>> axis;
        axis.reserve(ps.values.size());
        for (const auto &v : ps.values) {
            axis.emplace_back(ps.param_name, v);
        }
        scalar_axes.push_back(std::move(axis));
    }

    // Range-based generators expanded using the declared parameter type.
    std::unordered_map<std::string, AxisNumberKind> axis_kinds;
    for (const auto &param : function_params) {
        if (!param.name.empty() && param.decl != nullptr) {
            axis_kinds.emplace(param.name, classify_axis_type(param.decl->getType(), *result.Context));
        }
    }
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const auto report_axis_literal = [&](std::string_view origin, const std::string &name, std::string_view role, std::string_view text,
                                         std::string_view expected) {
        had_error_ = true;
        report(fmt::format("{} for '{}': {} '{}' is not {}; axis values are computed during code generation, so named constants and "
                           "expressions are not supported",
                           origin, name, role, text, expected));
    };
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const auto axis_int = [&](std::string_view origin, const std::string &name, std::string_view role,
                              std::string_view text) -> std::optional<long long> {
        auto value = parse_axis_integer(text);
        if (!value.has_value()) {
            report_axis_literal(origin, name, role, text, "an integer literal that fits in 'long long'");
        }
        return value;
    };
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const auto axis_double = [&](std::string_view origin, const std::string &name, std::string_view role,
                                 std::string_view text) -> std::optional<double> {
        auto value = parse_axis_floating_literal(text);
        if (!value.has_value()) {
            report_axis_literal(origin, name, role, text, "a finite numeric literal");
        }
        return value;
    };
    const auto axis_unsupported = [&](std::string_view origin, const std::string &name) {
        had_error_ = true;
        report(fmt::format("{} not supported for parameter type '{}' (parameter '{}'); expected an integer or floating-point type", origin,
                           param_types.at(name), name));
    };
    const auto axis_too_large = [&](std::string_view origin, const std::string &name) {
        had_error_ = true;
        report(fmt::format("{} for '{}' expands to more than {} values", origin, name, kMaxAxisValues));
    };
    const auto axis_count = [&](std::string_view origin, const std::string &name, std::string_view text) -> std::optional<long long> {
        auto count = axis_int(origin, name, "count", text);
        if (!count.has_value()) {
            return std::nullopt;
        }
        if (*count < 1) {
            count = 1;
        }
        if (std::cmp_greater(*count, kMaxAxisValues)) {
            axis_too_large(origin, name);
            return std::nullopt;
        }
        return count;
    };
    auto fmt_int    = [](long long v) -> std::string { return std::to_string(v); };
    auto fmt_double = [](double v) -> std::string {
        char buf[64];
        (void)std::snprintf(buf, sizeof(buf), "%.17g", v);
        return {buf};
    };
    for (const auto &rs : summary.parameter_ranges) {
        const AxisNumberKind                             kind = axis_kinds.at(rs.name);
        std::vector<std::pair<std::string, std::string>> axis;
        if (kind == AxisNumberKind::Integer) {
            const auto start = axis_int("parameters_range", rs.name, "start", rs.start);
            const auto step  = axis_int("parameters_range", rs.name, "step", rs.step);
            const auto end   = axis_int("parameters_range", rs.name, "end", rs.end);
            if (!start || !step || !end) {
                return;
            }
            const long long a  = *start;
            const long long st = *step == 0 ? 1 : *step;
            const long long b  = *end;
            if ((st > 0 && a > b) || (st < 0 && a < b)) {
                had_error_ = true;
                report(fmt::format("parameters_range for '{}' has inconsistent bounds", rs.name));
                return;
            }
            // Distances are computed in unsigned arithmetic so a bound near
            // the limits of long long cannot overflow the loop variable.
            const auto magnitude = st > 0 ? static_cast<unsigned long long>(st) : 0ULL - static_cast<unsigned long long>(st);
            for (long long v = a;;) {
                if (axis.size() >= kMaxAxisValues) {
                    axis_too_large("parameters_range", rs.name);
                    return;
                }
                axis.emplace_back(rs.name, fmt_int(v));
                const auto remaining = st > 0 ? static_cast<unsigned long long>(b) - static_cast<unsigned long long>(v)
                                              : static_cast<unsigned long long>(v) - static_cast<unsigned long long>(b);
                if (remaining < magnitude) {
                    break;
                }
                v += st;
            }
        } else if (kind == AxisNumberKind::Floating) {
            const auto start = axis_double("parameters_range", rs.name, "start", rs.start);
            const auto step  = axis_double("parameters_range", rs.name, "step", rs.step);
            const auto end   = axis_double("parameters_range", rs.name, "end", rs.end);
            if (!start || !step || !end) {
                return;
            }
            const double a  = *start;
            const double st = *step == 0.0 ? 1.0 : *step;
            const double b  = *end;
            if ((st > 0.0 && a > b) || (st < 0.0 && a < b)) {
                had_error_ = true;
                report(fmt::format("parameters_range for '{}' has inconsistent bounds", rs.name));
                return;
            }

            const double eps = std::abs(st) * 1e-12;
            for (std::size_t i = 0;; ++i) {
                const double v = a + st * static_cast<double>(i);
                if (!std::isfinite(v)) {
                    had_error_ = true;
                    report(fmt::format("parameters_range for '{}' produced a non-finite value", rs.name));
                    return;
                }
                if (st > 0.0) {
                    if (v > b + eps)
                        break;
                } else {
                    if (v < b - eps)
                        break;
                }
                if (axis.size() >= kMaxAxisValues) {
                    axis_too_large("parameters_range", rs.name);
                    return;
                }
                axis.emplace_back(rs.name, fmt_double(v));
            }
        } else {
            axis_unsupported("parameters_range", rs.name);
            return;
        }
        scalar_axes.push_back(std::move(axis));
    }
    for (const auto &ls : summary.parameter_linspaces) {
        const AxisNumberKind                             kind = axis_kinds.at(ls.name);
        std::vector<std::pair<std::string, std::string>> axis;
        if (kind == AxisNumberKind::Unsupported) {
            axis_unsupported("parameters_linspace", ls.name);
            return;
        }
        const auto count = axis_count("parameters_linspace", ls.name, ls.count);
        if (!count) {
            return;
        }
        const long long n = *count;
        if (kind == AxisNumberKind::Integer) {
            const auto start = axis_int("parameters_linspace", ls.name, "start", ls.start);
            const auto end   = axis_int("parameters_linspace", ls.name, "end", ls.end);
            if (!start || !end) {
                return;
            }
            const long long a = *start;
            const long long b = *end;
            if (n == 1) {
                axis.emplace_back(ls.name, fmt_int(a));
            } else {
                const double step = (static_cast<double>(b) - static_cast<double>(a)) / static_cast<double>(n - 1);
                for (long long i = 0; i < n; ++i) {
                    const double d = static_cast<double>(a) + step * static_cast<double>(i);
                    axis.emplace_back(ls.name, fmt_int(static_cast<long long>(std::llround(d))));
                }
            }
        } else {
            const auto start = axis_double("parameters_linspace", ls.name, "start", ls.start);
            const auto end   = axis_double("parameters_linspace", ls.name, "end", ls.end);
            if (!start || !end) {
                return;
            }
            const double a = *start;
            const double b = *end;
            if (n == 1) {
                axis.emplace_back(ls.name, fmt_double(a));
            } else {
                const double step = (b - a) / static_cast<double>(n - 1);
                for (long long i = 0; i < n; ++i) {
                    const double d = a + step * static_cast<double>(i);
                    axis.emplace_back(ls.name, fmt_double(d));
                }
            }
        }
        scalar_axes.push_back(std::move(axis));
    }
    for (const auto &gs : summary.parameter_geoms) {
        const AxisNumberKind                             kind = axis_kinds.at(gs.name);
        std::vector<std::pair<std::string, std::string>> axis;
        if (kind == AxisNumberKind::Unsupported) {
            axis_unsupported("parameters_geom", gs.name);
            return;
        }
        const auto count  = axis_count("parameters_geom", gs.name, gs.count);
        const auto start  = axis_double("parameters_geom", gs.name, "start", gs.start);
        const auto factor = axis_double("parameters_geom", gs.name, "factor", gs.factor);
        if (!count || !start || !factor) {
            return;
        }
        double v = *start;
        for (long long i = 0; i < *count; ++i) {
            if (!std::isfinite(v) || (kind == AxisNumberKind::Integer && std::abs(v) >= 9.2e18)) {
                had_error_ = true;
                report(fmt::format("parameters_geom for '{}' produced a value outside the representable range", gs.name));
                return;
            }
            axis.emplace_back(gs.name, kind == AxisNumberKind::Integer ? fmt_int(static_cast<long long>(std::llround(v))) : fmt_double(v));
            v *= *factor;
        }
        scalar_axes.push_back(std::move(axis));
    }
    for (const auto &ls : summary.parameter_logspaces) {
        const AxisNumberKind                             kind = axis_kinds.at(ls.name);
        std::vector<std::pair<std::string, std::string>> axis;
        if (kind == AxisNumberKind::Unsupported) {
            axis_unsupported("logspace", ls.name);
            return;
        }
        const auto count = axis_count("logspace", ls.name, ls.count);
        const auto base  = ls.base.empty() ? std::optional<double>{10.0} : axis_double("logspace", ls.name, "base", ls.base);
        const auto aexp  = axis_double("logspace", ls.name, "start exponent", ls.start_exp);
        const auto bexp  = axis_double("logspace", ls.name, "end exponent", ls.end_exp);
        if (!count || !base || !aexp || !bexp) {
            return;
        }
        const long long n    = *count;
        const double    step = n == 1 ? 0.0 : (*bexp - *aexp) / static_cast<double>(n - 1);
        for (long long i = 0; i < n; ++i) {
            const double val = std::pow(*base, *aexp + step * static_cast<double>(i));
            if (!std::isfinite(val) || (kind == AxisNumberKind::Integer && std::abs(val) >= 9.2e18)) {
                had_error_ = true;
                report(fmt::format("logspace for '{}' produced a value outside the representable range", ls.name));
                return;
            }
            axis.emplace_back(ls.name,
                              kind == AxisNumberKind::Integer ? fmt_int(static_cast<long long>(std::llround(val))) : fmt_double(val));
        }
        scalar_axes.push_back(std::move(axis));
    }

    auto join_csv = [](const std::vector<std::string> &parts) {
        std::string out;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i)
                out += ", ";
            out += parts[i];
        }
        return out;
    };

    using NameMap = std::map<std::string, std::string>;
    NameMap current;

    auto set_value = [&](const std::string &name, const std::string &value) -> std::optional<std::string> {
        auto it = current.find(name);
        if (it == current.end()) {
            current.emplace(name, value);
            return std::nullopt;
        }
        std::string prev = it->second;
        it->second       = value;
        return prev;
    };
    auto restore_value = [&](const std::string &name, const std::optional<std::string> &prev) {
        if (prev.has_value())
            current[name] = *prev;
        else
            current.erase(name);
    };

    auto emit_case = [&](const std::vector<std::string> &tpl_combo) {
        std::vector<std::string> value_exprs_for_display;
        std::vector<std::string> value_exprs_for_call;
        std::vector<FreeCallArg> free_call_args;
        free_call_args.reserve(function_params.size());
        auto case_fixture_types       = inferred_fixture_types;
        auto case_fixture_emit_types  = inferred_fixture_emit_types;
        auto case_fixture_entity_keys = inferred_fixture_entity_keys;

        for (const auto &param : function_params) {
            if (param.is_fixture && param.template_binding_index.has_value()) {
                const std::size_t binding_index = *param.template_binding_index;
                const std::string binding       = binding_index < tpl_combo.size() ? tpl_combo[binding_index] : std::string{};
                const auto        bound         = resolve_bound_fixture_type(binding, *func, *result.Context, policy);
                if (!bound.has_value()) {
                    had_error_ = true;
                    report(fmt::format("cannot use template binding '{}' as the fixture type of parameter '{}': it must be the plain "
                                       "(optionally qualified) name of a class, enum or type alias",
                                       binding, param.name.empty() ? std::string("<unnamed>") : param.name));
                    return;
                }
                if (module_importer_registration_ && bound->declaration != nullptr &&
                    !is_reachable_from_importer(*bound->declaration, *func, *result.Context)) {
                    had_error_ = true;
                    report(fmt::format("fixture type '{}' is not reachable after importing named module '{}'; the generated registration "
                                       "importer must name and instantiate this type",
                                       bound->type_name, owning_module_identity(*func)));
                    return;
                }
                case_fixture_types[param.fixture_index]      = bound->type_name;
                case_fixture_emit_types[param.fixture_index] = bound->type_name;
                case_fixture_entity_keys[param.fixture_index] =
                    bound->declaration != nullptr ? stable_entity_identity(*bound->declaration) : std::string{};
            }
            if (param.is_fixture) {
                FreeCallArg arg{};
                arg.kind          = FreeCallArgKind::Fixture;
                arg.fixture_index = param.fixture_index;
                free_call_args.push_back(std::move(arg));
                continue;
            }
            if (!param.is_value_axis) {
                // Parameter has a default argument and is not driven by explicit value axes.
                continue;
            }

            if (param.name.empty()) {
                had_error_ = true;
                report("missing value for unnamed function parameter in parameters(...) or parameters_pack(...)");
                return;
            }
            auto it = current.find(param.name);
            if (it == current.end()) {
                had_error_ = true;
                report(fmt::format("missing value for function parameter '{}' in parameters(...) or parameters_pack(...)", param.name));
                return;
            }
            auto kind = classify_type(param.type_spelling);
            // String parameters receive "..." literals verbatim. For any other
            // parameter type the quotes only protect an expression (or name a
            // single character) and are removed.
            const bool quoted_expression =
                kind != TypeKind::String && it->second.size() >= 2 && it->second.front() == '"' && it->second.back() == '"';
            std::string expr =
                quote_for_type(kind, quoted_expression ? decode_string_literal(it->second) : it->second, param.type_spelling);
            if (module_importer_registration_ &&
                !validate_importer_binding(*func, expr, "parameter expression", *result.Context, [&](const std::string &message) {
                    had_error_ = true;
                    report(message);
                })) {
                return;
            }
            value_exprs_for_call.push_back(expr);
            value_exprs_for_display.push_back(expr);
            FreeCallArg arg{};
            arg.kind             = FreeCallArgKind::Value;
            arg.value_expression = std::move(expr);
            free_call_args.push_back(std::move(arg));
        }

        const std::string display_args = join_csv(value_exprs_for_display);
        const std::string call_args    = join_csv(value_exprs_for_call);
        add_case(tpl_combo, display_args, call_args, case_fixture_types, case_fixture_emit_types, case_fixture_entity_keys,
                 inferred_fixture_required_scopes, free_call_args);
    };

    std::function<void(std::size_t, const std::vector<std::string> &)> visit_scalars = [&](std::size_t                     idx,
                                                                                           const std::vector<std::string> &tpl_combo) {
        if (idx == scalar_axes.size()) {
            emit_case(tpl_combo);
            return;
        }
        for (const auto &nv : scalar_axes[idx]) {
            const auto prev = set_value(nv.first, nv.second);
            visit_scalars(idx + 1, tpl_combo);
            restore_value(nv.first, prev);
        }
    };

    std::function<void(std::size_t, const std::vector<std::string> &)> visit_packs = [&](std::size_t                     idx,
                                                                                         const std::vector<std::string> &tpl_combo) {
        if (idx == summary.param_packs.size()) {
            if (scalar_axes.empty())
                emit_case(tpl_combo);
            else
                visit_scalars(0, tpl_combo);
            return;
        }
        const auto &pp = summary.param_packs[idx];
        for (const auto &row : pp.rows) {
            std::vector<std::pair<std::string, std::optional<std::string>>> prev;
            prev.reserve(pp.names.size());
            for (std::size_t i = 0; i < pp.names.size(); ++i) {
                prev.emplace_back(pp.names[i], set_value(pp.names[i], row[i]));
            }
            visit_packs(idx + 1, tpl_combo);
            for (auto &it : std::ranges::reverse_view(prev)) {
                restore_value(it.first, it.second);
            }
        }
    };

    for (const auto &tpl_combo : combined_tpl_combos) {
        if (summary.param_packs.empty()) {
            if (scalar_axes.empty())
                emit_case(tpl_combo);
            else
                visit_scalars(0, tpl_combo);
        } else {
            visit_packs(0, tpl_combo);
        }
    }
}

bool TestCaseCollector::has_errors() const { return had_error_; }

FixtureDeclCollector::FixtureDeclCollector(std::vector<FixtureDeclInfo> &out, bool header_declaration_registration,
                                           bool module_importer_registration)
    : out_(out), header_declaration_registration_(header_declaration_registration),
      module_importer_registration_(module_importer_registration) {}

void FixtureDeclCollector::report(const clang::CXXRecordDecl &decl, const clang::SourceManager &sm, std::string_view message) const {
    const SourceLocation  loc     = sm.getSpellingLoc(decl.getBeginLoc());
    const llvm::StringRef file    = sm.getFilename(loc);
    const unsigned        line    = sm.getSpellingLineNumber(loc);
    const std::string     subject = decl.getQualifiedNameAsString();
    const std::string     locpfx  = !file.empty() ? fmt::format("{}:{}: ", file.str(), line) : std::string{};
    const std::string     subj    = !subject.empty() ? fmt::format(" ({})", subject) : std::string{};
    log_err("gentest_codegen: {}{}{}\n", locpfx, message, subj);
}

void FixtureDeclCollector::run(const MatchFinder::MatchResult &result) {
    const auto *record = result.Nodes.getNodeAs<CXXRecordDecl>("gentest.fixture");
    if (record == nullptr) {
        return;
    }
    if (!record->isThisDeclarationADefinition()) {
        return;
    }
    const auto *sm  = result.SourceManager;
    auto        loc = record->getBeginLoc();
    if (loc.isInvalid()) {
        return;
    }
    if (loc.isMacroID()) {
        loc = sm->getExpansionLoc(loc);
    }
    if (sm->isInSystemHeader(loc) || sm->isWrittenInBuiltinFile(loc)) {
        return;
    }

    const auto attrs = collect_gentest_attributes_for(*record, *sm);
    if (header_declaration_registration_ && sm->isWrittenInMainFile(loc) && !is_header_like_path(sm->getFilename(sm->getFileLoc(loc))) &&
        (!attrs.gentest.empty() || !attrs.mis_scoped_gentest.empty())) {
        had_error_ = true;
        report(*record, *sm,
               "Gentest fixture attributes in an authored .cpp are not supported by header-declaration registration; annotate the header "
               "definition instead");
        return;
    }

    for (const auto &message : attrs.mis_scoped_gentest) {
        had_error_ = true;
        report(*record, *sm, mis_scoped_gentest_message(message));
    }
    for (const auto &message : attrs.other_namespaces) {
        report(*record, *sm, fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
    }
    auto summary = validate_fixture_attributes(attrs.gentest, [&](const std::string &m) {
        had_error_ = true;
        report(*record, *sm, m);
    });

    if (summary.had_error) {
        had_error_ = true;
        return;
    }

    if (summary.lifetime != FixtureLifetime::MemberSuite && summary.lifetime != FixtureLifetime::MemberGlobal) {
        return; // not a shared fixture declaration
    }
    const auto        suite_override = find_suite_override(record->getDeclContext(), *sm, suite_cache_, had_error_);
    const std::string suite_path     = suite_override.value_or(derive_namespace_path(record->getDeclContext()));
    if (summary.lifetime == FixtureLifetime::MemberSuite && suite_path.empty()) {
        had_error_ = true;
        report(*record, *sm, "'fixture(suite)' requires an enclosing named namespace to derive a suite path");
        return;
    }

    FixtureDeclInfo info{};
    info.qualified_name         = record->getQualifiedNameAsString();
    info.importer_reachable     = !module_importer_registration_ || is_importer_reachable_entity(*record);
    info.registration_type_name = info.importer_reachable ? info.qualified_name : std::string{};
    info.base_name              = record->getNameAsString();
    info.namespace_parts        = collect_namespace_parts(record->getDeclContext());
    info.suite_name             = suite_path;
    info.scope                  = (summary.lifetime == FixtureLifetime::MemberSuite) ? FixtureScope::Suite : FixtureScope::Global;
    {
        const SourceLocation  tu_loc  = sm->getLocForStartOfFile(sm->getMainFileID());
        const llvm::StringRef tu_file = sm->getFilename(tu_loc);
        info.tu_filename              = tu_file.str();
    }
    {
        const SourceLocation  file_loc = sm->getFileLoc(record->getLocation());
        const llvm::StringRef file     = sm->getFilename(file_loc);
        info.filename                  = file.str();
        info.line                      = sm->getSpellingLineNumber(file_loc);
        info.declaration_site_key      = declaration_site_identity(*record, *sm, "fixture");
        info.entity_key                = stable_entity_identity(*record);
        ODRHash odr_hash;
        odr_hash.AddCXXRecordDecl(record);
        info.semantic_fingerprint = fmt::format("entity:{}|odr:{}|module:{}|{}|{}|{}|{}", info.entity_key, odr_hash.CalculateHash(),
                                                owning_module_identity(*record), info.qualified_name, info.suite_name,
                                                static_cast<int>(info.scope), info.base_name);
    }
    out_.push_back(std::move(info));
}

bool FixtureDeclCollector::has_errors() const { return had_error_; }

namespace {
struct ParsedFixtureType {
    std::string base;
    std::string suffix;
    std::string full;
    bool        qualified = false;
};

struct FixtureLookup {
    std::unordered_map<std::string, std::vector<FixtureDeclInfo *>> by_base;
    std::unordered_map<std::string, FixtureDeclInfo *>              by_qualified;
};

std::string trim_copy(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

ParsedFixtureType parse_fixture_type(std::string_view text) {
    ParsedFixtureType out{};
    out.full          = trim_copy(text);
    int         depth = 0;
    std::size_t split = std::string::npos;
    for (std::size_t i = 0; i < out.full.size(); ++i) {
        char ch = out.full[i];
        if (ch == '<') {
            if (depth == 0) {
                split = i;
                break;
            }
            ++depth;
        } else if (ch == '>') {
            if (depth > 0) {
                --depth;
            }
        }
    }
    if (split == std::string::npos) {
        out.base = trim_copy(out.full);
    } else {
        out.base   = trim_copy(out.full.substr(0, split));
        out.suffix = out.full.substr(split);
    }
    out.qualified = out.base.find("::") != std::string::npos;
    return out;
}

std::string strip_leading_colons(std::string_view text) {
    if (text.starts_with("::")) {
        return std::string(text.substr(2));
    }
    return std::string(text);
}

bool is_prefix(const std::vector<std::string> &prefix, const std::vector<std::string> &value) {
    if (prefix.size() > value.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (prefix[i] != value[i]) {
            return false;
        }
    }
    return true;
}

std::string join_namespace(const std::vector<std::string> &parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out.append("::");
        out.append(parts[i]);
    }
    return out;
}

auto build_fixture_lookups(std::vector<FixtureDeclInfo> &fixtures) -> std::unordered_map<std::string, FixtureLookup> {
    std::unordered_map<std::string, FixtureLookup> lookups;
    for (auto &fixture : fixtures) {
        auto &lookup = lookups[fixture.tu_filename];
        lookup.by_base[fixture.base_name].push_back(&fixture);
        if (!fixture.qualified_name.empty()) {
            lookup.by_qualified[fixture.qualified_name] = &fixture;
        }
    }
    return lookups;
}

bool is_visible_fixture_for_test(const FixtureDeclInfo &fixture, const TestCaseInfo &test) {
    return is_prefix(fixture.namespace_parts, test.namespace_parts);
}

auto find_best_visible_fixture(const FixtureLookup &lookup, const std::string &base_name, const TestCaseInfo &test) -> FixtureDeclInfo * {
    const auto it = lookup.by_base.find(base_name);
    if (it == lookup.by_base.end()) {
        return nullptr;
    }

    FixtureDeclInfo *best = nullptr;
    for (auto *decl : it->second) {
        if (!is_visible_fixture_for_test(*decl, test)) {
            continue;
        }
        if (!best || decl->namespace_parts.size() > best->namespace_parts.size()) {
            best = decl;
        }
    }
    return best;
}

std::string derive_local_fixture_type_name(const ParsedFixtureType &parsed, const TestCaseInfo &test) {
    if (parsed.qualified) {
        return parsed.full;
    }

    static constexpr std::array builtin_types{std::string_view{"bool"},
                                              std::string_view{"char"},
                                              std::string_view{"char8_t"},
                                              std::string_view{"char16_t"},
                                              std::string_view{"char32_t"},
                                              std::string_view{"wchar_t"},
                                              std::string_view{"signed char"},
                                              std::string_view{"short"},
                                              std::string_view{"short int"},
                                              std::string_view{"int"},
                                              std::string_view{"long"},
                                              std::string_view{"long int"},
                                              std::string_view{"long long"},
                                              std::string_view{"long long int"},
                                              std::string_view{"unsigned char"},
                                              std::string_view{"unsigned short"},
                                              std::string_view{"unsigned short int"},
                                              std::string_view{"unsigned"},
                                              std::string_view{"unsigned int"},
                                              std::string_view{"unsigned long"},
                                              std::string_view{"unsigned long int"},
                                              std::string_view{"unsigned long long"},
                                              std::string_view{"unsigned long long int"},
                                              std::string_view{"float"},
                                              std::string_view{"double"},
                                              std::string_view{"long double"}};
    if (std::ranges::find(builtin_types, parsed.base) != builtin_types.end()) {
        return parsed.full;
    }

    const std::string ns_prefix = join_namespace(test.namespace_parts);
    if (ns_prefix.empty()) {
        return parsed.base + parsed.suffix;
    }
    return ns_prefix + "::" + parsed.base + parsed.suffix;
}
} // namespace

bool resolve_free_fixtures(std::vector<TestCaseInfo> &cases, std::vector<FixtureDeclInfo> &fixtures) {
    const auto                                         lookups = build_fixture_lookups(fixtures);
    std::unordered_map<std::string, FixtureDeclInfo *> fixtures_by_entity;
    for (auto &fixture : fixtures) {
        if (!fixture.entity_key.empty()) {
            fixtures_by_entity.try_emplace(fixture.entity_key, &fixture);
        }
    }

    bool       ok     = true;
    const auto report = [&](const TestCaseInfo &test, const std::string &message) {
        ok = false;
        log_err("gentest_codegen: {}:{}: {} (test {})\n", test.filename, test.line, message, test.display_name);
    };
    const auto scope_name = [](FixtureScope scope) -> std::string_view {
        switch (scope) {
        case FixtureScope::Local: return "local";
        case FixtureScope::Suite: return "suite";
        case FixtureScope::Global: return "global";
        }
        return "unknown";
    };

    for (auto &test : cases) {
        test.registration_headers.clear();
        if (test.free_fixture_types.empty()) {
            continue;
        }
        test.free_fixtures.clear();
        const auto           it     = lookups.find(test.tu_filename);
        const FixtureLookup *lookup = (it == lookups.end()) ? nullptr : &it->second;
        for (std::size_t fixture_idx = 0; fixture_idx < test.free_fixture_types.size(); ++fixture_idx) {
            const std::string &raw_type = test.free_fixture_types[fixture_idx];
            const std::string &emit_type =
                (fixture_idx < test.free_fixture_emit_types.size()) ? test.free_fixture_emit_types[fixture_idx] : raw_type;
            const std::optional<FixtureScope> required_scope =
                (fixture_idx < test.free_fixture_required_scopes.size()) ? test.free_fixture_required_scopes[fixture_idx] : std::nullopt;
            const std::string fixture_entity =
                (fixture_idx < test.free_fixture_entity_keys.size()) ? test.free_fixture_entity_keys[fixture_idx] : std::string{};
            ParsedFixtureType parsed = parse_fixture_type(raw_type);
            if (parsed.base.empty()) {
                report(test, "empty inferred fixture type from function signature");
                continue;
            }
            const std::string base_no_colons = strip_leading_colons(parsed.base);
            const auto        emit_local     = [&](const std::string &qualified_name) {
                if (required_scope.has_value()) {
                    report(test,
                           fmt::format(
                               "fixture '{}' is declared as '{}' but resolved as local; ensure the fixture declaration is visible and in "
                               "an ancestor namespace",
                               parsed.full, scope_name(*required_scope)));
                    return;
                }
                FreeFixtureUse use{};
                use.type_name = qualified_name;
                use.scope     = FixtureScope::Local;
                test.free_fixtures.push_back(std::move(use));
            };
            const auto emit_declared = [&](FixtureDeclInfo *decl) {
                if (required_scope.has_value() && decl->scope != *required_scope) {
                    report(test, fmt::format("fixture '{}' declared as '{}' but resolved as '{}' via '{}'", parsed.full,
                                             scope_name(*required_scope), scope_name(decl->scope), decl->qualified_name));
                    return;
                }
                FreeFixtureUse    use{};
                const std::string visible_type = (emit_type == raw_type) ? std::string{} : parse_fixture_type(emit_type).full;
                use.type_name                  = visible_type.empty() ? (decl->qualified_name + parsed.suffix) : visible_type;
                use.registry_name              = decl->qualified_name + parsed.suffix;
                use.scope                      = decl->scope;
                use.suite_name                 = decl->suite_name;
                test.free_fixtures.push_back(std::move(use));
                if (!decl->importer_reachable && !visible_type.empty() &&
                    (decl->registration_type_name.empty() || visible_type < decl->registration_type_name)) {
                    decl->registration_type_name = visible_type;
                }
                if (!decl->filename.empty() &&
                    std::ranges::find(test.registration_headers, decl->filename) == test.registration_headers.end()) {
                    test.registration_headers.push_back(decl->filename);
                }
            };

            if (!fixture_entity.empty()) {
                const auto entity_it = fixtures_by_entity.find(fixture_entity);
                if (entity_it != fixtures_by_entity.end()) {
                    FixtureDeclInfo *decl = entity_it->second;
                    if (!is_visible_fixture_for_test(*decl, test)) {
                        report(test, fmt::format("fixture '{}' is not in an ancestor namespace of this test", parsed.full));
                        continue;
                    }
                    emit_declared(decl);
                    continue;
                }
            }

            // An inferred fixture type with a known entity is spelled from its
            // canonical type, so an unqualified name is a global-namespace
            // type: never re-qualify it with the test's namespace and never
            // bind it to a different declaration that shares its base name.
            const bool canonical_spelling = parsed.qualified || !fixture_entity.empty();
            if (lookup) {
                if (canonical_spelling) {
                    const auto qit = lookup->by_qualified.find(base_no_colons);
                    if (qit != lookup->by_qualified.end()) {
                        FixtureDeclInfo *decl = qit->second;
                        if (!is_visible_fixture_for_test(*decl, test)) {
                            report(test, fmt::format("fixture '{}' is not in an ancestor namespace of this test", base_no_colons));
                            continue;
                        }
                        emit_declared(decl);
                        continue;
                    }
                    emit_local(parse_fixture_type(emit_type).full);
                    continue;
                }

                FixtureDeclInfo *best = find_best_visible_fixture(*lookup, parsed.base, test);
                if (best) {
                    emit_declared(best);
                    continue;
                }
                if (lookup->by_base.contains(parsed.base)) {
                    report(test, fmt::format("fixture '{}' is not in an ancestor namespace of this test", parsed.base));
                    continue;
                }
            }

            if (canonical_spelling) {
                emit_local(parse_fixture_type(emit_type).full);
                continue;
            }
            emit_local(derive_local_fixture_type_name(parse_fixture_type(emit_type), test));
        }

        if (test.returns_async) {
            const auto shared_fixture_count = std::ranges::count_if(
                test.free_fixtures, [](const FreeFixtureUse &fixture) { return fixture.scope != FixtureScope::Local; });
            if (shared_fixture_count > 1) {
                report(
                    test,
                    "async tests may depend on at most one suite/global fixture parameter; multiple shared fixture parameters would make "
                    "execution order parameter-dependent");
            }
        }
    }

    for (const auto &fixture : fixtures) {
        if (!fixture.importer_reachable && fixture.registration_type_name.empty()) {
            ok = false;
            log_err("gentest_codegen: {}:{}: shared fixture '{}' is not reachable after importing its named module; export the fixture "
                    "definition or use an exported alias from a registered case\n",
                    fixture.filename, fixture.line, fixture.qualified_name);
        }
    }

    return ok;
}

bool resolve_free_fixtures(std::vector<TestCaseInfo> &cases, const std::vector<FixtureDeclInfo> &fixtures) {
    auto mutable_fixtures = fixtures;
    return resolve_free_fixtures(cases, mutable_fixtures);
}

} // namespace gentest::codegen
