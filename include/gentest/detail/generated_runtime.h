#pragma once

// Unstable generated-code support contract. Generated registration wrappers and
// generated-like regression TUs include this when they need fixture binding,
// shared-fixture registration/lookup, and case registration in one place.

#include "gentest/detail/fixture_api.h"
#include "gentest/detail/registration_runtime.h"
#include "gentest/detail/runtime_support.h"

#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gentest::detail {

enum class SharedFixtureScope {
    Suite,
    Global,
};

using SharedFixtureCreateFn = std::shared_ptr<void> (*)(std::string_view suite, std::string &error);
using SharedFixturePhaseFn  = void (*)(void *instance, std::string &error);

// Runtime registry entry points used by generated shared-fixture bindings.
GENTEST_RUNTIME_API void register_shared_fixture(SharedFixtureScope scope, std::string_view suite, std::string_view fixture_name,
                                                 SharedFixtureCreateFn create, SharedFixturePhaseFn setup, SharedFixturePhaseFn teardown);

GENTEST_RUNTIME_API std::shared_ptr<void> get_shared_fixture(SharedFixtureScope scope, std::string_view suite,
                                                             std::string_view fixture_name, std::string &error);

template <typename T> class ErasedDeleter {
  public:
    ErasedDeleter() noexcept                            = default;
    ErasedDeleter(ErasedDeleter &&) noexcept            = default;
    ErasedDeleter &operator=(ErasedDeleter &&) noexcept = default;
    ErasedDeleter(const ErasedDeleter &)                = delete;
    ErasedDeleter &operator=(const ErasedDeleter &)     = delete;

    template <typename U, typename D>
    explicit ErasedDeleter(std::in_place_type_t<U>, D &&deleter)
        : impl_(std::make_unique<Model<U, std::decay_t<D>>>(std::forward<D>(deleter))) {}

    void operator()(T *ptr) noexcept {
        if (impl_) {
            impl_->call(ptr);
        } else {
            delete ptr;
        }
    }

  private:
    struct Concept {
        virtual ~Concept()                 = default;
        virtual void call(T *ptr) noexcept = 0;
    };

    template <typename U, typename D> struct Model final : Concept {
        explicit Model(D &&deleter) : deleter(std::move(deleter)) {}
        void call(T *ptr) noexcept override { deleter(static_cast<U *>(ptr)); }
        D    deleter;
    };

    std::unique_ptr<Concept> impl_;
};

template <typename T> struct FixtureAllocation {
    using UniquePtr = std::unique_ptr<T, ErasedDeleter<T>>;
    UniquePtr          unique;
    std::shared_ptr<T> shared;

    bool valid() const { return static_cast<bool>(shared) || static_cast<bool>(unique); }
    T   *get() const { return shared ? shared.get() : unique.get(); }
};

template <typename T, typename = void> struct HasGentestAllocate : std::false_type {};
template <typename T> struct HasGentestAllocate<T, std::void_t<decltype(T::gentest_allocate())>> : std::true_type {};

template <typename T> inline constexpr bool kHasGentestAllocate = HasGentestAllocate<T>::value;

template <typename T, typename = void> struct HasGentestAllocateWithSuite : std::false_type {};
template <typename T>
struct HasGentestAllocateWithSuite<T, std::void_t<decltype(T::gentest_allocate(std::declval<std::string_view>()))>> : std::true_type {};

template <typename T> inline constexpr bool kHasGentestAllocateWithSuite = HasGentestAllocateWithSuite<T>::value;

template <typename> inline constexpr bool kAlwaysFalse = false;

template <typename> struct IsUniquePtr : std::false_type {};
template <typename U, typename D> struct IsUniquePtr<std::unique_ptr<U, D>> : std::true_type {};

template <typename T, typename U, typename D> typename FixtureAllocation<T>::UniquePtr adopt_unique(std::unique_ptr<U, D> &&ptr) {
    using Ptr = typename std::unique_ptr<U, D>::pointer;
    static_assert(std::is_convertible_v<Ptr, T *>, "gentest_allocate unique_ptr pointer must be convertible to T*");
    auto deleter = std::move(ptr.get_deleter());
    T   *raw     = static_cast<T *>(ptr.release());
    return typename FixtureAllocation<T>::UniquePtr(raw, ErasedDeleter<T>(std::in_place_type<U>, std::move(deleter)));
}

template <typename T, typename U> typename FixtureAllocation<T>::UniquePtr adopt_raw(U *ptr) {
    static_assert(std::is_convertible_v<U *, T *>, "gentest_allocate raw pointer must be convertible to T*");
    T *raw = static_cast<T *>(ptr);
    return typename FixtureAllocation<T>::UniquePtr(raw, ErasedDeleter<T>(std::in_place_type<U>, std::default_delete<U>{}));
}

template <typename T, typename Result> void assign_allocation(FixtureAllocation<T> &out, Result &&result) {
    using ResultT = std::decay_t<Result>;
    if constexpr (IsUniquePtr<ResultT>::value) {
        out.unique = adopt_unique<T>(std::forward<Result>(result));
    } else if constexpr (std::is_pointer_v<ResultT> && std::is_convertible_v<ResultT, T *>) {
        out.unique = adopt_raw<T>(result);
    } else if constexpr (std::is_constructible_v<std::shared_ptr<T>, ResultT>) {
        out.shared = std::shared_ptr<T>(std::forward<Result>(result));
    } else {
        static_assert(kAlwaysFalse<ResultT>, "gentest_allocate must return std::unique_ptr<T, D>, std::shared_ptr<T>, or T*");
    }
}

template <typename T, typename Result> FixtureAllocation<T> allocate_fixture_from(Result &&result) {
    FixtureAllocation<T> out;
    assign_allocation(out, std::forward<Result>(result));
    return out;
}

template <typename T> FixtureAllocation<T> allocate_fixture() {
    if constexpr (kHasGentestAllocate<T>) {
        return allocate_fixture_from<T>(T::gentest_allocate());
    } else if constexpr (kHasGentestAllocateWithSuite<T>) {
        return allocate_fixture_from<T>(T::gentest_allocate(std::string_view{}));
    } else {
        FixtureAllocation<T> out;
        out.unique = typename FixtureAllocation<T>::UniquePtr(new T());
        return out;
    }
}

template <typename T> FixtureAllocation<T> allocate_fixture(std::string_view suite) {
    if constexpr (kHasGentestAllocateWithSuite<T>) {
        return allocate_fixture_from<T>(T::gentest_allocate(suite));
    } else {
        return allocate_fixture<T>();
    }
}

template <typename T> class FixtureHandle {
  public:
    using element_type = T;

    FixtureHandle() : storage_(allocate_fixture<T>()) {}
    explicit FixtureHandle(std::string_view suite) : storage_(allocate_fixture<T>(suite)) {}

    static FixtureHandle empty() { return FixtureHandle(EmptyTag{}); }

    bool init() {
        storage_ = allocate_fixture<T>();
        return storage_.valid();
    }

    bool init(std::string_view suite) {
        storage_ = allocate_fixture<T>(suite);
        return storage_.valid();
    }

    bool init_shared(std::shared_ptr<T> shared) {
        storage_.shared = std::move(shared);
        storage_.unique = typename FixtureAllocation<T>::UniquePtr{};
        return storage_.valid();
    }

    bool valid() const { return storage_.valid(); }

    T *get() { return storage_.get(); }

    T &ref() { return *get(); }

    std::shared_ptr<T> shared() {
        if (!storage_.shared && storage_.unique) {
            storage_.shared = std::shared_ptr<T>(std::move(storage_.unique));
        }
        return storage_.shared;
    }

    operator T &() { return ref(); }
    operator T *() { return get(); }
    operator std::shared_ptr<T>() { return shared(); }

  private:
    struct EmptyTag {
        explicit EmptyTag() = default;
    };
    explicit FixtureHandle(EmptyTag) {}

    FixtureAllocation<T> storage_;
};

namespace detail_internal {

template <typename Fixture> inline std::shared_ptr<void> shared_fixture_create(std::string_view suite, std::string &error) {
#if GENTEST_EXCEPTIONS_ENABLED
    try {
        auto handle = FixtureHandle<Fixture>::empty();
        if (!handle.init(suite)) {
            error = "returned null";
            return {};
        }
        return handle.shared();
    } catch (const std::exception &e) {
        error = std::string("std::exception: ") + e.what();
        return {};
    } catch (...) {
        error = "unknown exception";
        return {};
    }
#else
    auto handle = FixtureHandle<Fixture>::empty();
    if (!handle.init(suite)) {
        error = "returned null";
        return {};
    }
    return handle.shared();
#endif
}

template <typename Fixture> inline void shared_fixture_setup(void *instance, std::string &error) {
    if constexpr (std::is_base_of_v<gentest::FixtureSetup, Fixture>) {
        if (!instance) {
            error = "instance missing";
            return;
        }
#if GENTEST_EXCEPTIONS_ENABLED
        try {
            static_cast<Fixture *>(instance)->setUp();
        } catch (const gentest::assertion &e) { error = e.message(); } catch (const std::exception &e) {
            error = std::string("std::exception: ") + e.what();
        } catch (...) { error = "unknown exception"; }
#else
        static_cast<Fixture *>(instance)->setUp();
#endif
    }
}

template <typename Fixture> inline void shared_fixture_teardown(void *instance, std::string &error) {
    if constexpr (std::is_base_of_v<gentest::FixtureTearDown, Fixture>) {
        if (!instance) {
            error = "instance missing";
            return;
        }
#if GENTEST_EXCEPTIONS_ENABLED
        try {
            static_cast<Fixture *>(instance)->tearDown();
        } catch (const gentest::assertion &e) { error = e.message(); } catch (const std::exception &e) {
            error = std::string("std::exception: ") + e.what();
        } catch (...) { error = "unknown exception"; }
#else
        static_cast<Fixture *>(instance)->tearDown();
#endif
    }
}

} // namespace detail_internal

template <typename Fixture>
inline void register_shared_fixture(SharedFixtureScope scope, std::string_view suite, std::string_view fixture_name) {
    register_shared_fixture(scope, suite, fixture_name, &detail_internal::shared_fixture_create<Fixture>,
                            &detail_internal::shared_fixture_setup<Fixture>, &detail_internal::shared_fixture_teardown<Fixture>);
}

template <typename Fixture>
inline std::shared_ptr<Fixture> get_shared_fixture_typed(SharedFixtureScope scope, std::string_view suite, std::string_view fixture_name,
                                                         std::string &error) {
    auto raw = get_shared_fixture(scope, suite, fixture_name, error);
    return std::static_pointer_cast<Fixture>(raw);
}

} // namespace gentest::detail
