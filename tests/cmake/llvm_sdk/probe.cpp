#include <clang/Basic/Module.h>
#include <clang/Basic/Version.h>
#if __has_include(<clang/UnifiedSymbolResolution/USRGeneration.h>)
#include <clang/UnifiedSymbolResolution/USRGeneration.h>
#else
#include <clang/Index/USRGeneration.h>
#endif
#include <llvm/ADT/SmallString.h>

// Exercise the installed SDK's export representation and USR link symbol.
bool inspectModule(const clang::Module &module, const clang::Decl &declaration) {
    for (const auto &entry : module.Exports) {
#if CLANG_VERSION_MAJOR >= 23
        const clang::Module *exported = entry.first;
#else
        const clang::Module *exported = entry.getPointer();
#endif
        if (exported != nullptr) {
            llvm::SmallString<128> usr;
            return clang::index::generateUSRForDecl(&declaration, usr);
        }
    }

    return false;
}

int main() {}
