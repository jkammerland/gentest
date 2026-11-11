#include "mock_type_printer.hpp"

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/TemplateName.h>
#include <llvm/ADT/APSInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gentest::codegen {
namespace {

using clang::QualType;

[[nodiscard]] clang::PrintingPolicy fallback_policy(const clang::ASTContext &ctx) {
    auto policy = clang::PrintingPolicy(ctx.getLangOpts());
    policy.adjustForCPlusPlus();
    policy.SuppressScope          = false;
    policy.FullyQualifiedName     = true;
    policy.SuppressUnwrittenScope = false;
    return policy;
}

// Builds C++ type spellings declarator-first: `inner` is the part of the
// declarator that has already been printed (for example `*` or `(&)[2]`),
// and every type node wraps it before handing it to its own element type.
class GlobalTypePrinter {
  public:
    GlobalTypePrinter(const clang::ASTContext &ctx, MockTypeSpelling spelling)
        : ctx_(ctx), spelling_(spelling), policy_(fallback_policy(ctx)) {}

    [[nodiscard]] std::string print_with_declarator(QualType type, std::string declarator) const {
        if (spelling_ == MockTypeSpelling::Canonical) {
            type = ctx_.getCanonicalType(type);
        }
        return print(type, std::move(declarator));
    }

  private:
    [[nodiscard]] std::string print(QualType type, std::string inner) const {
        if (type.isNull()) {
            return inner;
        }
        type = peel_transparent_sugar(type);

        const clang::SplitQualType split = type.split();
        const clang::Type         *node  = split.Ty;
        const clang::Qualifiers    quals = split.Quals;

        if (const auto *pointer = llvm::dyn_cast<clang::PointerType>(node)) {
            return print_pointer_like(pointer->getPointeeType(), "*", quals, inner);
        }
        if (const auto *reference = llvm::dyn_cast<clang::ReferenceType>(node)) {
            const char *sigil = llvm::isa<clang::LValueReferenceType>(reference) ? "&" : "&&";
            return print_pointer_like(reference->getPointeeType(), sigil, quals, inner);
        }
        if (const auto *member_pointer = llvm::dyn_cast<clang::MemberPointerType>(node)) {
            const auto *record = member_pointer->getMostRecentCXXRecordDecl();
            const auto  owner  = record != nullptr ? print_tag_decl(*record) : std::nullopt;
            if (!owner) {
                return print_fallback(type, inner);
            }
            return print_pointer_like(member_pointer->getPointeeType(), *owner + "::*", quals, inner);
        }
        if (const auto *array = llvm::dyn_cast<clang::ArrayType>(node)) {
            return print_array(*array, quals, type, std::move(inner));
        }
        if (const auto *function = llvm::dyn_cast<clang::FunctionProtoType>(node)) {
            return print_function(*function, std::move(inner));
        }
        if (const auto *tag = llvm::dyn_cast<clang::TagType>(node)) {
            if (const auto name = print_tag_decl(*tag->getDecl())) {
                return leaf(*name, quals, inner);
            }
            return print_fallback(type, inner);
        }
        if (const auto *typedef_type = llvm::dyn_cast<clang::TypedefType>(node)) {
            // peel_transparent_sugar() only keeps typedefs whose scope is printable.
            if (const auto name = print_named_decl(*typedef_type->getDecl())) {
                return leaf(*name, quals, inner);
            }
            return print_fallback(type, inner);
        }
        if (const auto *specialization = llvm::dyn_cast<clang::TemplateSpecializationType>(node)) {
            if (const auto name = print_template_name(specialization->getTemplateName())) {
                return leaf(*name + print_written_template_arguments(specialization->template_arguments()), quals, inner);
            }
            return print_fallback(type, inner);
        }
        if (const auto *parameter = llvm::dyn_cast<clang::TemplateTypeParmType>(node)) {
            if (const auto *identifier = parameter->getIdentifier()) {
                return leaf(identifier->getName().str(), quals, inner);
            }
            return print_fallback(type, inner);
        }
        if (const auto *expansion = llvm::dyn_cast<clang::PackExpansionType>(node)) {
            std::string text = print(expansion->getPattern(), {}) + "...";
            if (!inner.empty()) {
                text += ' ';
                text += inner;
            }
            return text;
        }
        return print_fallback(type, inner);
    }

    // Typedefs and template specializations are kept as written when their
    // names can be spelled from the global namespace. Every other sugar node
    // (elaborated keywords, parentheses, attributes, substituted template
    // parameters, using-declarations, deduced and decltype types) is looked
    // through.
    [[nodiscard]] bool keeps_sugar(const clang::Type &node) const {
        if (const auto *typedef_type = llvm::dyn_cast<clang::TypedefType>(&node)) {
            const auto *decl = typedef_type->getDecl();
            return !decl->getDeclContext()->isDependentContext() && print_named_decl(*decl).has_value();
        }
        return llvm::isa<clang::TemplateSpecializationType>(node);
    }

    [[nodiscard]] QualType peel_transparent_sugar(QualType type) const {
        while (!type.isNull() && !keeps_sugar(*type.getTypePtr())) {
            const QualType next = type.getSingleStepDesugaredType(ctx_);
            if (next == type) {
                break;
            }
            type = next;
        }
        return type;
    }

    [[nodiscard]] bool needs_declarator_parens(QualType pointee) const {
        const auto *node = peel_transparent_sugar(pointee).getTypePtrOrNull();
        return node != nullptr && (llvm::isa<clang::FunctionType>(node) || llvm::isa<clang::ArrayType>(node));
    }

    [[nodiscard]] std::string qualifier_text(clang::Qualifiers quals) const { return quals.getAsString(policy_); }

    [[nodiscard]] std::string leaf(const std::string &name, clang::Qualifiers quals, const std::string &inner) const {
        std::string text;
        if (!quals.empty()) {
            text = qualifier_text(quals);
            text += ' ';
        }
        text += name;
        if (!inner.empty()) {
            // Match Clang's spelling: `int[2]`, but `int *` and `void (int)`.
            if (inner.front() != '[') {
                text += ' ';
            }
            text += inner;
        }
        return text;
    }

    [[nodiscard]] std::string print_pointer_like(QualType pointee, std::string sigil, clang::Qualifiers quals,
                                                 const std::string &inner) const {
        std::string declarator = std::move(sigil);
        if (!quals.empty()) {
            declarator += qualifier_text(quals);
            if (!inner.empty()) {
                declarator += ' ';
            }
        }
        declarator += inner;
        if (needs_declarator_parens(pointee)) {
            declarator = "(" + declarator + ")";
        }
        return print(pointee, std::move(declarator));
    }

    [[nodiscard]] std::string print_array(const clang::ArrayType &array, clang::Qualifiers quals, QualType type, std::string inner) const {
        std::string bound;
        if (const auto *constant = llvm::dyn_cast<clang::ConstantArrayType>(&array)) {
            bound = std::to_string(constant->getSize().getZExtValue());
        } else if (const auto *dependent = llvm::dyn_cast<clang::DependentSizedArrayType>(&array)) {
            if (dependent->getSizeExpr() == nullptr) {
                return print_fallback(type, inner);
            }
            bound = print_expression(*dependent->getSizeExpr());
        } else if (!llvm::isa<clang::IncompleteArrayType>(&array)) {
            return print_fallback(type, inner);
        }
        inner += "[" + bound + "]";
        // cv-qualifiers on an array type apply to its elements.
        return print(ctx_.getQualifiedType(array.getElementType(), quals), std::move(inner));
    }

    [[nodiscard]] std::string print_function(const clang::FunctionProtoType &function, std::string inner) const {
        std::string params;
        for (unsigned i = 0; i < function.getNumParams(); ++i) {
            if (i != 0) {
                params += ", ";
            }
            params += print(function.getParamType(i), {});
        }
        if (function.isVariadic()) {
            params += function.getNumParams() == 0 ? "..." : ", ...";
        }

        inner += "(" + params + ")";
        const clang::Qualifiers method_quals = function.getMethodQuals();
        if (!method_quals.empty()) {
            inner += ' ';
            inner += qualifier_text(method_quals);
        }
        switch (function.getRefQualifier()) {
        case clang::RQ_LValue: inner += " &"; break;
        case clang::RQ_RValue: inner += " &&"; break;
        case clang::RQ_None: break;
        }
        switch (function.getExceptionSpecType()) {
        case clang::EST_DynamicNone:
        case clang::EST_BasicNoexcept:
        case clang::EST_NoexceptTrue: inner += " noexcept"; break;
        case clang::EST_DependentNoexcept:
            if (const auto *expr = function.getNoexceptExpr()) {
                inner += " noexcept(" + print_expression(*expr) + ")";
            }
            break;
        default: break;
        }
        return print(function.getReturnType(), std::move(inner));
    }

    [[nodiscard]] std::string print_fallback(QualType type, const std::string &inner) const {
        std::string              text;
        llvm::raw_string_ostream os(text);
        type.print(os, policy_);
        os.flush();
        if (!inner.empty()) {
            text += ' ';
            text += inner;
        }
        return text;
    }

    [[nodiscard]] std::string print_expression(const clang::Expr &expr) const {
        std::string              text;
        llvm::raw_string_ostream os(text);
        expr.printPretty(os, nullptr, policy_);
        os.flush();
        return text;
    }

    // Returns the `a::b::` scope of `decl` without the leading `::`, or
    // nullopt when the declaration cannot be named from the global namespace
    // (local classes, members of unnamed classes, ...). Anonymous namespaces
    // are skipped: their members are found through the enclosing namespace.
    [[nodiscard]] std::optional<std::string> print_scope(const clang::NamedDecl &decl) const {
        std::vector<std::string> parts;
        clang::DeclarationName   name_in_scope = decl.getDeclName();
        for (const clang::DeclContext *context = decl.getDeclContext(); context != nullptr; context = context->getParent()) {
            if (context->isTranslationUnit()) {
                break;
            }
            if (context->isFunctionOrMethod()) {
                return std::nullopt;
            }
            if (const auto *ns = llvm::dyn_cast<clang::NamespaceDecl>(context)) {
                if (ns->isAnonymousNamespace() || ns->isRedundantInlineQualifierFor(name_in_scope)) {
                    continue;
                }
                parts.push_back(ns->getName().str());
                name_in_scope = ns->getDeclName();
                continue;
            }
            if (const auto *record = llvm::dyn_cast<clang::CXXRecordDecl>(context)) {
                if (record->getIdentifier() == nullptr) {
                    return std::nullopt;
                }
                std::string part = record->getName().str();
                if (const auto *spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
                    part += print_specialization_arguments(*spec);
                }
                parts.push_back(std::move(part));
                name_in_scope = record->getDeclName();
                continue;
            }
            if (context->isTransparentContext()) {
                continue;
            }
            return std::nullopt;
        }

        std::string scope;
        for (const auto &part : llvm::reverse(parts)) {
            scope += part;
            scope += "::";
        }
        return scope;
    }

    [[nodiscard]] std::optional<std::string> print_named_decl(const clang::NamedDecl &decl) const {
        if (decl.getIdentifier() == nullptr) {
            return std::nullopt;
        }
        auto scope = print_scope(decl);
        if (!scope) {
            return std::nullopt;
        }
        return "::" + *scope + decl.getName().str();
    }

    [[nodiscard]] std::optional<std::string> print_tag_decl(const clang::TagDecl &decl) const {
        if (decl.getIdentifier() == nullptr) {
            if (const auto *typedef_decl = decl.getTypedefNameForAnonDecl()) {
                return print_named_decl(*typedef_decl);
            }
            return std::nullopt;
        }
        auto name = print_named_decl(decl);
        if (!name) {
            return std::nullopt;
        }
        if (const auto *spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(&decl)) {
            *name += print_specialization_arguments(*spec);
        }
        return name;
    }

    [[nodiscard]] std::optional<std::string> print_template_name(clang::TemplateName name) const {
        const auto *decl = name.getAsTemplateDecl();
        if (decl == nullptr) {
            return std::nullopt;
        }
        if (llvm::isa<clang::TemplateTemplateParmDecl>(decl)) {
            if (decl->getIdentifier() == nullptr) {
                return std::nullopt;
            }
            return decl->getName().str();
        }
        if (decl->getDeclContext()->isDependentContext()) {
            return std::nullopt;
        }
        return print_named_decl(*decl);
    }

    // Arguments of an instantiated class template specialization. Trailing
    // arguments that only repeat the parameter's default are omitted, matching
    // Clang's own printing (`std::vector<int>` rather than spelling out the
    // allocator).
    [[nodiscard]] std::string print_specialization_arguments(const clang::ClassTemplateSpecializationDecl &spec) const {
        const llvm::ArrayRef<clang::TemplateArgument> args   = spec.getTemplateArgs().asArray();
        const clang::TemplateParameterList           *params = spec.getSpecializedTemplate()->getTemplateParameters();

        std::size_t count = args.size();
        while (count > 0) {
            const std::size_t index = count - 1;
            if (index >= params->size() ||
                !clang::isSubstitutedDefaultArgument(const_cast<clang::ASTContext &>(ctx_), args[index], params->getParam(index),
                                                     args.take_front(index), params->getDepth())) {
                break;
            }
            --count;
        }

        std::vector<std::string> printed;
        for (std::size_t i = 0; i < count; ++i) {
            const bool include_type = i >= params->size() ||
                                      clang::TemplateParameterList::shouldIncludeTypeForArgument(policy_, params, static_cast<unsigned>(i));
            append_template_argument(printed, args[i], include_type);
        }
        return join_template_arguments(printed);
    }

    [[nodiscard]] std::string print_written_template_arguments(llvm::ArrayRef<clang::TemplateArgument> args) const {
        std::vector<std::string> printed;
        for (const auto &arg : args) {
            append_template_argument(printed, arg, /*include_type=*/true);
        }
        return join_template_arguments(printed);
    }

    [[nodiscard]] static std::string join_template_arguments(const std::vector<std::string> &printed) {
        std::string text = "<";
        for (std::size_t i = 0; i < printed.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += printed[i];
        }
        text += '>';
        return text;
    }

    void append_template_argument(std::vector<std::string> &out, const clang::TemplateArgument &arg, bool include_type) const {
        switch (arg.getKind()) {
        case clang::TemplateArgument::Null: return;
        case clang::TemplateArgument::Pack:
            for (const auto &element : arg.pack_elements()) {
                append_template_argument(out, element, include_type);
            }
            return;
        case clang::TemplateArgument::Type: out.push_back(print(arg.getAsType(), {})); return;
        case clang::TemplateArgument::NullPtr: out.emplace_back("nullptr"); return;
        case clang::TemplateArgument::Template:
            if (auto name = print_template_name(arg.getAsTemplate())) {
                out.push_back(std::move(*name));
                return;
            }
            break;
        case clang::TemplateArgument::TemplateExpansion:
            if (auto name = print_template_name(arg.getAsTemplateOrTemplatePattern())) {
                out.push_back(std::move(*name) + "...");
                return;
            }
            break;
        case clang::TemplateArgument::Declaration:
            if (auto name = print_declaration_argument(arg)) {
                out.push_back(std::move(*name));
                return;
            }
            break;
        case clang::TemplateArgument::Integral:
            if (auto value = print_enum_argument(arg)) {
                out.push_back(std::move(*value));
                return;
            }
            break;
        default: break;
        }
        std::string              text;
        llvm::raw_string_ostream os(text);
        arg.print(policy_, os, include_type);
        os.flush();
        out.push_back(std::move(text));
    }

    [[nodiscard]] std::optional<std::string> print_declaration_argument(const clang::TemplateArgument &arg) const {
        const auto *decl = arg.getAsDecl();
        if (decl == nullptr || llvm::isa<clang::TemplateParamObjectDecl>(decl)) {
            return std::nullopt;
        }
        auto name = print_named_decl(*decl);
        if (!name) {
            return std::nullopt;
        }
        // Reference parameters bind to the entity itself; pointer and
        // pointer-to-member parameters take its address.
        if (arg.getParamTypeForDecl()->isReferenceType()) {
            return name;
        }
        return "&" + *name;
    }

    [[nodiscard]] std::optional<std::string> print_enum_argument(const clang::TemplateArgument &arg) const {
        const auto *enum_type = arg.getIntegralType()->getAs<clang::EnumType>();
        if (enum_type == nullptr) {
            return std::nullopt;
        }
        const auto *decl = enum_type->getDecl()->getDefinition();
        const auto  type = print(arg.getIntegralType(), {});
        if (decl != nullptr) {
            const llvm::APSInt value = arg.getAsIntegral();
            for (const auto *enumerator : decl->enumerators()) {
                if (llvm::APSInt::isSameValue(enumerator->getInitVal(), value) && enumerator->getIdentifier() != nullptr) {
                    return type + "::" + enumerator->getName().str();
                }
            }
        }
        llvm::SmallString<32> digits;
        arg.getAsIntegral().toString(digits);
        return "static_cast<" + type + ">(" + std::string(digits) + ")";
    }

    const clang::ASTContext &ctx_;
    MockTypeSpelling         spelling_;
    clang::PrintingPolicy    policy_;
};

} // namespace

std::string print_mock_type(clang::QualType type, const clang::ASTContext &ctx, MockTypeSpelling spelling, std::string_view declarator) {
    return GlobalTypePrinter(ctx, spelling).print_with_declarator(type, std::string(declarator));
}

} // namespace gentest::codegen
