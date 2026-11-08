#include "cases.hpp"

#include "gentest/detail/runtime_context.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace mocking {

void interface_returns() {
    gentest::mock<Calculator> mock_calc;
    EXPECT_CALL(mock_calc, compute).times(1).returns(42);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(12, 30);
    EXPECT_EQ(result, 42);
}

} // namespace mocking

namespace mocking {

void interface_returns_ref() {
    gentest::mock<RefProvider> mock_ref;
    int                        storage = 7;
    EXPECT_CALL(mock_ref, value).times(1).returns_ref(storage);

    RefProvider *iface  = &mock_ref;
    int         &result = iface->value();
    EXPECT_EQ(&result, &storage);
    result = 9;
    EXPECT_EQ(storage, 9);
}

} // namespace mocking

namespace mocking {

void interface_returns_matches() {
    gentest::mock<Calculator> mock_calc;
    EXPECT_CALL(mock_calc, compute).with(12, 30).returns(42);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(12, 30);
    EXPECT_EQ(result, 42);
}

} // namespace mocking

namespace mocking {

void interface_reset() {
    gentest::mock<Calculator> mock_calc;
    int                       resets = 0;
    EXPECT_CALL(mock_calc, reset).times(2).invokes([&]() { ++resets; });

    Calculator *iface = &mock_calc;
    iface->reset();
    iface->reset();

    EXPECT_EQ(resets, 2);
}

} // namespace mocking

namespace mocking {

void interface_non_default_ctor() {
    gentest::mock<NeedsInit> mock_clock{5};
    EXPECT_CALL(mock_clock, now).times(1).returns(123);

    NeedsInit *iface = &mock_clock;
    EXPECT_EQ(iface->now(), 123);
}

namespace {
int              colliding_seen = 0;
::asserts::Check colliding_check{.id = 9};
} // namespace

// Generated mock signatures name ::detail, ::async, ::asserts, ::match and ::approx
// types from inside namespace gentest; they only compile when every nested name
// is globally qualified (#265).
void interface_colliding_namespaces() {
    gentest::mock<CollidingNamespaces> mock_iface;
    int                                taken = 0;
    EXPECT_CALL(mock_iface, take).times(1).invokes([&](::detail::Handle handle) { taken = handle.id; });
    EXPECT_CALL(mock_iface, next).times(1).returns(::async::Job{.id = 7});
    EXPECT_CALL(mock_iface, share).times(1).invokes([](const std::vector<::async::Job> &jobs) {
        return std::make_shared<::detail::Handle>(::detail::Handle{.id = static_cast<int>(jobs.size())});
    });
    EXPECT_CALL(mock_iface, table).times(1).invokes([](const std::pair<::approx::Tolerance, ::detail::Kind> &key) {
        std::map<int, std::optional<::match::Pattern>> out;
        out[key.second == ::detail::Kind::second ? 2 : 1] = ::match::Pattern{.id = 3};
        return out;
    });
    int done_calls = 0;
    EXPECT_CALL(mock_iface, on_done).times(1).invokes([&](std::optional<void (*)(const ::async::Job &, ::detail::Handle *)> callback) {
        ::detail::Handle handle{.id = 1};
        (*callback)(::async::Job{.id = 2}, &handle);
        ++done_calls;
    });
    EXPECT_CALL(mock_iface, find).times(1).invokes([](const std::vector<::asserts::Check *(*)(::match::Pattern)> &pickers) {
        return pickers.at(0)(::match::Pattern{});
    });
    EXPECT_CALL(mock_iface, handles).times(1).invokes([](const std::vector<int ::detail::Handle::*> &members) {
        auto out              = std::make_unique<::detail::Handle[]>(1);
        out[0].*members.at(0) = 11;
        return out;
    });
    EXPECT_CALL(mock_iface, tagged<::detail::Kind::second>).times(1).returns(40);
    EXPECT_CALL(mock_iface, defaulted<::detail::Handle>).times(1);
    EXPECT_CALL(mock_iface, collect<long>).times(1).invokes([](long value, const std::vector<::async::Job> &jobs) {
        return std::vector<::detail::Handle>(static_cast<std::size_t>(value) + jobs.size());
    });

    CollidingNamespaces *iface = &mock_iface;
    iface->take(::detail::Handle{.id = 5});
    EXPECT_EQ(taken, 5);
    EXPECT_EQ(iface->next().id, 7);
    EXPECT_EQ(iface->share({::async::Job{}, ::async::Job{}})->id, 2);
    EXPECT_EQ(iface->table({::approx::Tolerance{}, ::detail::Kind::second}).at(2)->id, 3);
    iface->on_done(+[](const ::async::Job &job, ::detail::Handle *handle) { colliding_seen = job.id + handle->id; });
    EXPECT_EQ(colliding_seen, 3);
    EXPECT_EQ(done_calls, 1);
    EXPECT_EQ(iface->find({+[](::match::Pattern) { return &colliding_check; }}), &colliding_check);
    EXPECT_EQ(iface->handles({&::detail::Handle::id})[0].id, 11);
    EXPECT_EQ(mock_iface.collect(2L, std::vector<::async::Job>(1)).size(), std::size_t{3});
    EXPECT_EQ(mock_iface.tagged<::detail::Kind::second>(::detail::Handle{.id = 1}), 40);
    mock_iface.defaulted(::detail::Handle{});
}

void interface_colliding_template_argument() {
    gentest::mock<Store<::detail::Handle>> mock_store;
    EXPECT_CALL(mock_store, get).times(1).returns(::detail::Handle{.id = 4});
    std::size_t stored = 0;
    EXPECT_CALL(mock_store, put).times(1).invokes([&](const std::vector<::detail::Handle> &values) { stored = values.size(); });

    Store<::detail::Handle> *iface = &mock_store;
    EXPECT_EQ(iface->get().id, 4);
    iface->put({::detail::Handle{}, ::detail::Handle{}});
    EXPECT_EQ(stored, std::size_t{2});
}

namespace {
int declarator_seen = 0;
int declarator_rows[2][2]{{1, 2}, {3, 4}};

void declarator_record(int value) { declarator_seen = value; }
} // namespace

// Parameter names must be placed inside the declarator of function-pointer,
// array-reference and member-function-pointer parameters (#276).
void interface_declarator_parameters() {
    gentest::mock<Declarators> mock_iface;
    EXPECT_CALL(mock_iface, set_callback).times(1).invokes([](void (*cb)(int)) { cb(7); });
    // Actions receive decayed arguments, so array references arrive as pointers.
    EXPECT_CALL(mock_iface, sum).times(1).invokes([](const int *values) { return values[0] + values[1] + values[2]; });
    EXPECT_CALL(mock_iface, apply).times(1).invokes([](int (DeclaratorTarget::*method)(int) const, int arg) {
        const DeclaratorTarget target{.value = 3};
        return (target.*method)(arg);
    });
    EXPECT_CALL(mock_iface, aliased).times(1).invokes([](DeclaratorCallback cb) {
        cb(11);
        return cb;
    });
    int unnamed_calls = 0;
    EXPECT_CALL(mock_iface, unnamed).times(1).invokes([&](void (*cb)(int), int *values) {
        values[1] = 9;
        cb(values[0] + values[1]);
        ++unnamed_calls;
    });

    Declarators *iface = &mock_iface;
    iface->set_callback(&declarator_record);
    EXPECT_EQ(declarator_seen, 7);
    const int values[3]{1, 2, 3};
    EXPECT_EQ(iface->sum(values), 6);
    EXPECT_EQ(iface->apply(&DeclaratorTarget::scaled, 5), 15);
    EXPECT_EQ(iface->aliased(&declarator_record), &declarator_record);
    EXPECT_EQ(declarator_seen, 11);
    int pair[2]{4, 0};
    iface->unnamed(&declarator_record, pair);
    EXPECT_EQ(pair[1], 9);
    EXPECT_EQ(declarator_seen, 13);
    EXPECT_EQ(unnamed_calls, 1);
}

// Function-pointer, array-pointer and member-function-pointer return types
// cannot be written in front of the method name (#276).
void interface_declarator_returns() {
    gentest::mock<Declarators> mock_iface;
    EXPECT_CALL(mock_iface, get_callback).times(1).returns(&declarator_record);
    EXPECT_CALL(mock_iface, row).times(1).invokes([](int index) { return &declarator_rows[index]; });
    EXPECT_CALL(mock_iface, pick).times(1).returns(&DeclaratorTarget::scaled);

    Declarators *iface = &mock_iface;
    void (*cb)(int)    = iface->get_callback();
    cb(21);
    EXPECT_EQ(declarator_seen, 21);
    int (*selected)[2] = iface->row(1);
    EXPECT_EQ((*selected)[0] + (*selected)[1], 7);
    const DeclaratorTarget target{.value = 2};
    EXPECT_EQ((target.*(iface->pick()))(4), 8);
}

void interface_declarator_template_and_ctor() {
    gentest::mock<Declarators> mock_iface;
    // EXPECT_CALL cannot take a template-id with a top-level comma.
    gentest::expect<&Declarators::first<int, 3>>(mock_iface, "first").times(1).invokes([](int *values) { return values[2]; });
    int values[3]{5, 6, 7};
    EXPECT_EQ(mock_iface.first(values), 7);

    gentest::mock<DeclaratorCtor> mock_ctor(&declarator_record, {20, 22});
    EXPECT_EQ(declarator_seen, 42);
    EXPECT_CALL(mock_ctor, get).times(1).returns(5);
    const DeclaratorCtor &iface = mock_ctor;
    EXPECT_EQ(iface.get(), 5);
}

} // namespace mocking

namespace mocking {

void concrete_invokes() {
    gentest::mock<Ticker> mock_tick;
    int                   observed = 0;
    EXPECT_CALL(mock_tick, tick).times(3).invokes([&](int v) { observed += v; });

    mock_tick.tick(1);
    mock_tick.tick(2);
    mock_tick.tick(3);

    EXPECT_EQ(observed, 6);
}

} // namespace mocking

namespace mocking {

void concrete_non_default_ctor() {
    gentest::mock<NoDefault> mock_nd{7};
    EXPECT_CALL(mock_nd, work).times(1).with(3);

    mock_nd.work(3);
}

} // namespace mocking

namespace mocking {

void concrete_static_member() {
    gentest::mock<Ticker> mock_tick;
    EXPECT_CALL(mock_tick, add).times(1).returns(123);

    EXPECT_EQ(mock_tick.add(4, 5), 123);
}

} // namespace mocking

namespace mocking {

void concrete_invokes_matches() {
    gentest::mock<Ticker> mock_tick;
    int                   observed = 0;
    EXPECT_CALL(mock_tick, tick).times(3).with(1).invokes([&](int v) { observed += v; });

    mock_tick.tick(1);
    mock_tick.tick(1);
    mock_tick.tick(1);

    EXPECT_EQ(observed, 3);
}

} // namespace mocking

namespace mocking {

void concrete_predicate_match() {
    gentest::mock<Ticker> mock_tick;
    int                   sum = 0;
    // Accept only even values
    EXPECT_CALL(mock_tick, tick).times(2).where_args([](int v) { return v % 2 == 0; }).invokes([&](int v) { sum += v; });

    mock_tick.tick(2);
    mock_tick.tick(4);

    EXPECT_EQ(sum, 6);
}

} // namespace mocking

namespace mocking {

void concrete_template_member_expect_int() {
    gentest::mock<Ticker> mock_tick;
    int                   sum = 0;
    EXPECT_CALL(mock_tick, tadd<int>).times(2).with(5).invokes([&](int v) { sum += v; });

    mock_tick.tadd(5);
    mock_tick.tadd(5);

    EXPECT_EQ(sum, 10);
}

} // namespace mocking

namespace mocking {

void concrete_template_member_signature_collision() {
    gentest::mock<Ticker> mock_tick;
    int                   tick_sum = 0;
    int                   tadd_sum = 0;

    EXPECT_CALL(mock_tick, tick).times(1).with(2).invokes([&](int v) { tick_sum += v; });
    EXPECT_CALL(mock_tick, tadd<int>).times(2).with(5).invokes([&](int v) { tadd_sum += v; });

    mock_tick.tick(2);
    mock_tick.tadd(5);
    mock_tick.tadd(5);

    EXPECT_EQ(tick_sum, 2);
    EXPECT_EQ(tadd_sum, 10);
}

} // namespace mocking

namespace mocking {

void concrete_template_member_instantiation_split() {
    gentest::mock<Ticker> mock_tick;
    int                   int_sum  = 0;
    long                  long_sum = 0;

    EXPECT_CALL(mock_tick, tadd<int>).times(1).with(4).invokes([&](int v) { int_sum += v; });
    EXPECT_CALL(mock_tick, tadd<long>).times(1).with(6L).invokes([&](long v) { long_sum += v; });

    mock_tick.tadd(4);
    mock_tick.tadd(6L);

    EXPECT_EQ(int_sum, 4);
    EXPECT_EQ(long_sum, 6L);
}

} // namespace mocking

namespace mocking {

void concrete_direct_expect_signature_collision() {
    gentest::mock<Ticker> mock_tick;
    int                   tick_sum = 0;
    int                   tadd_sum = 0;

    gentest::expect<&Ticker::tick>(mock_tick, "::mocking::Ticker::tick").times(1).with(3).invokes([&](int v) { tick_sum += v; });
    EXPECT_CALL(mock_tick, tadd<int>).times(2).with(7).invokes([&](int v) { tadd_sum += v; });

    mock_tick.tick(3);
    mock_tick.tadd(7);
    mock_tick.tadd(7);

    EXPECT_EQ(tick_sum, 3);
    EXPECT_EQ(tadd_sum, 14);
}

} // namespace mocking

namespace mocking {

void concrete_direct_constant_expect_signature_collision() {
    gentest::mock<Ticker> mock_tick;
    int                   tick_sum = 0;
    int                   tadd_sum = 0;

    gentest::expect<&Ticker::tick>(mock_tick, "::mocking::Ticker::tick").times(1).with(4).invokes([&](int v) { tick_sum += v; });
    gentest::expect<&Ticker::tadd<int>>(mock_tick, "::mocking::Ticker::tadd<int>").times(2).with(9).invokes([&](int v) { tadd_sum += v; });

    mock_tick.tick(4);
    mock_tick.tadd(9);
    mock_tick.tadd(9);

    EXPECT_EQ(tick_sum, 4);
    EXPECT_EQ(tadd_sum, 18);
}

} // namespace mocking

namespace mocking {

void template_forwarding_alias() {
    gentest::mock<ForwardingAlias> mock_alias;
    TrackedMove                    value;
    int                            calls = 0;
    EXPECT_CALL(mock_alias, take<TrackedMove &>).times(1).invokes([&](const TrackedMove &) { ++calls; });

    mock_alias.template take<TrackedMove &>(value);

    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(value.moved);

    gentest::mock<ForwardingAlias> rvalue_mock;
    EXPECT_CALL(rvalue_mock, take<TrackedMove>).times(1).invokes([&](const TrackedMove &actual) {
        EXPECT_EQ(&actual, &value);
        ++calls;
    });
    rvalue_mock.template take<TrackedMove>(std::move(value));
    EXPECT_EQ(calls, 2);
    // Inspecting the move-tracking flag proves that reference binding did not move from the argument.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_FALSE(value.moved);
}

} // namespace mocking

namespace mocking {

void direct_unique_template_member_expect() {
    gentest::mock<ForwardingAlias> mock_alias;
    TrackedMove                    value;
    int                            calls = 0;

    gentest::expect(mock_alias, &ForwardingAlias::template take<TrackedMove &>).times(1).invokes([&](const TrackedMove &) { ++calls; });

    mock_alias.template take<TrackedMove &>(value);

    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(value.moved);
}

} // namespace mocking

namespace mocking {

void template_template_member_expect() {
    gentest::mock<TemplateTemplateFixed> mock_fixed;
    int                                  calls = 0;

    gentest::expect(mock_fixed, &TemplateTemplateFixed::template take<std::array>).times(1).invokes([&](std::array<int, 2> value) {
        calls += value[0];
    });

    mock_fixed.template take<std::array>(std::array<int, 2>{1, 2});

    EXPECT_EQ(calls, 1);
}

} // namespace mocking

namespace mocking {

void template_template_ctor() {
    gentest::mock<TemplateTemplateCtorTarget> mock_target{std::array<int, 2>{1, 2}};
    EXPECT_CALL(mock_target, ping).times(1);

    mock_target.ping();
}

} // namespace mocking

namespace mocking {

void template_template_pack_direct_expect() {
    gentest::mock<TemplateTemplatePacker> mock_packer;
    int                                   calls = 0;

    gentest::expect<&TemplateTemplatePacker::template join<std::vector, std::list>>(
        mock_packer, "::mocking::TemplateTemplatePacker::join<std::vector, std::list>")
        .times(1)
        .invokes([&](const std::tuple<std::vector<int>, std::list<int>> &value) {
            calls += static_cast<int>(std::get<0>(value).size() + std::get<1>(value).size());
        });

    const auto value = std::tuple{std::vector<int>{1, 2}, std::list<int>{3}};
    mock_packer.template join<std::vector, std::list>(value);

    EXPECT_EQ(calls, 3);
}

} // namespace mocking

namespace mocking {

void crtp_bridge() {
    gentest::mock<DerivedRunner> mock_runner;
    std::vector<int>             captured;
    EXPECT_CALL(mock_runner, handle).times(2).invokes([&](int v) { captured.push_back(v); });

    mock_runner.handle(7);
    mock_runner.handle(11);

    EXPECT_EQ(captured.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(captured[0], 7);
    EXPECT_EQ(captured[1], 11);
}

} // namespace mocking

namespace mocking {

void crtp_bridge_matches() {
    gentest::mock<DerivedRunner> mock_runner;
    int                          count = 0;
    EXPECT_CALL(mock_runner, handle).with(7).times(2).invokes([&](int) { ++count; });

    mock_runner.handle(7);
    mock_runner.handle(7);

    EXPECT_EQ(count, 2);
}

} // namespace mocking

namespace mocking {

void matchers_eq_any() {
    using namespace gentest::match;
    gentest::mock<Calculator> mock_calc;
    EXPECT_CALL(mock_calc, compute).times(1).where(Eq(12), Any()).returns(300);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(12, 999);
    EXPECT_EQ(result, 300);
}

} // namespace mocking

namespace mocking {

void matchers_in_range() {
    using namespace gentest::match;
    gentest::mock<Ticker> mock_tick;
    int                   count = 0;
    EXPECT_CALL(mock_tick, tick).times(2).where_args(InRange(5, 10)).invokes([&](int) { ++count; });

    mock_tick.tick(5);
    mock_tick.tick(10);
    EXPECT_EQ(count, 2);
}

} // namespace mocking

namespace mocking {

void matchers_not() {
    using namespace gentest::match;
    gentest::mock<Ticker> mock_tick;
    int                   sum = 0;
    EXPECT_CALL(mock_tick, tick).times(2).where_args(Not(Eq(0))).invokes([&](int v) { sum += v; });

    mock_tick.tick(1);
    mock_tick.tick(2);
    EXPECT_EQ(sum, 3);
}

} // namespace mocking

namespace mocking {

void matchers_where_call() {
    gentest::mock<Calculator> mock_calc;
    EXPECT_CALL(mock_calc, compute).times(1).where_call([](int lhs, int rhs) { return ((lhs + rhs) % 2) == 0; }).returns(42);

    Calculator *iface  = &mock_calc;
    const int   result = iface->compute(1, 3); // even sum
    EXPECT_EQ(result, 42);
}

} // namespace mocking

namespace mocking {

void move_only_with_eq() {
    gentest::mock<MOConsumer> mock_mo;
    int                       hits = 0;
    EXPECT_CALL(mock_mo, accept).times(1).with(MoveOnly{7}).invokes([&](const MoveOnly &) { ++hits; });

    EXPECT_CALL(mock_mo, accept_rvalue).times(1).with(MoveOnly{9}).invokes([&](const MoveOnly &actual) { hits += actual.value; });

    mock_mo.accept_rvalue(MoveOnly{9});
    mock_mo.accept(MoveOnly{7});
    EXPECT_EQ(hits, 10);
}

} // namespace mocking

namespace mocking {

void move_only_refwrap_by_value() {
    gentest::mock<RefWrapConsumer> mock_wrap;
    EXPECT_CALL(mock_wrap, take).times(1);

    mock_wrap.take(RefWrap<int &>{});
    EXPECT_TRUE(true);
}

} // namespace mocking

namespace mocking {

void matchers_str_contains() {
    using namespace gentest::match;
    gentest::mock<Stringer> mock_str;
    int                     hits = 0;
    EXPECT_CALL(mock_str, put).times(2).where_args(StrContains("abc")).invokes([&](const std::string &) { ++hits; });

    mock_str.put("xxabcxx");
    mock_str.put("abc");
    EXPECT_EQ(hits, 2);
}

} // namespace mocking

namespace mocking {

void matchers_starts_ends() {
    using namespace gentest::match;
    gentest::mock<Stringer> mock_str;
    int                     hits = 0;
    EXPECT_CALL(mock_str, put).times(1).where_args(AllOf(StartsWith("hello"), EndsWith("!"))).invokes([&](const std::string &) { ++hits; });

    mock_str.put("hello world!");
    EXPECT_EQ(hits, 1);
}

} // namespace mocking

namespace mocking {

void matchers_cstr_null_safe() {
    using namespace gentest::match;

    auto contains = StrContains("abc").make<const char *>();
    EXPECT_TRUE(static_cast<bool>(contains.test));
    EXPECT_TRUE(static_cast<bool>(contains.describe));
    EXPECT_TRUE(!contains.test(nullptr));
    EXPECT_TRUE(contains.describe(nullptr).find("null") != std::string::npos);
    EXPECT_TRUE(contains.test("xxabcxx"));

    auto starts = StartsWith("abc").make<const char *>();
    EXPECT_TRUE(static_cast<bool>(starts.test));
    EXPECT_TRUE(static_cast<bool>(starts.describe));
    EXPECT_TRUE(!starts.test(nullptr));
    EXPECT_TRUE(starts.describe(nullptr).find("null") != std::string::npos);
    EXPECT_TRUE(starts.test("abcdef"));

    auto ends = EndsWith("xyz").make<const char *>();
    EXPECT_TRUE(static_cast<bool>(ends.test));
    EXPECT_TRUE(static_cast<bool>(ends.describe));
    EXPECT_TRUE(!ends.test(nullptr));
    EXPECT_TRUE(ends.describe(nullptr).find("null") != std::string::npos);
    EXPECT_TRUE(ends.test("123xyz"));
}

} // namespace mocking

namespace mocking {

void matchers_near() {
    using namespace gentest::match;
    gentest::mock<Floater> mock_fl;
    int                    hits = 0;
    EXPECT_CALL(mock_fl, feed).times(2).where_args(Near(3.14, 0.01)).invokes([&](double) { ++hits; });

    mock_fl.feed(3.14);
    mock_fl.feed(3.149);
    EXPECT_EQ(hits, 2);
}

} // namespace mocking

namespace mocking {

void matchers_ge_anyof() {
    using namespace gentest::match;
    gentest::mock<Ticker> mock_tick;
    int                   count = 0;
    EXPECT_CALL(mock_tick, tick).times(2).where_args(Ge(5)).invokes([&](int) { ++count; });

    mock_tick.tick(5);
    mock_tick.tick(7);
    EXPECT_EQ(count, 2);
}

} // namespace mocking

namespace mocking {

void concurrency_adopted_ordered_dispatch() {
    gentest::mock<Calculator> mock_calc;
    std::mutex                gate_mtx;
    std::condition_variable   gate_cv;
    int                       first_action_entries = 0;
    bool                      first_release        = false;
    bool                      second_done          = false;
    std::array<int, 2>        results{0, 0};
    std::atomic<bool>         first_args_ok{false};

    EXPECT_CALL(mock_calc, compute).times(1).with(1, 1).invokes([&](int lhs, int rhs) {
        first_args_ok.store(lhs == 1 && rhs == 1, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(gate_mtx);
            ++first_action_entries;
        }
        gate_cv.notify_all();
        std::unique_lock<std::mutex> lk(gate_mtx);
        gate_cv.wait(lk, [&] { return first_release; });
        return 11;
    });
    EXPECT_CALL(mock_calc, compute).times(1).with(1, 1).returns(22);

    auto        context = gentest::get_current_context();
    Calculator *iface   = &mock_calc;
    std::thread t1([&] {
        auto guard = gentest::set_current_context(context);
        results[0] = iface->compute(1, 1);
    });

    {
        std::unique_lock<std::mutex> lk(gate_mtx);
        gate_cv.wait(lk, [&] { return first_action_entries == 1; });
    }

    std::thread t2([&] {
        auto guard = gentest::set_current_context(context);
        results[1] = iface->compute(1, 1);
        {
            std::lock_guard<std::mutex> lk(gate_mtx);
            second_done = true;
        }
        gate_cv.notify_all();
    });

    {
        std::unique_lock<std::mutex> lk(gate_mtx);
        gate_cv.wait(lk, [&] { return first_action_entries == 2 || second_done; });
        first_release = true;
    }
    gate_cv.notify_all();

    t1.join();
    t2.join();

    EXPECT_EQ(first_action_entries, 1);
    EXPECT_TRUE(first_args_ok.load(std::memory_order_acquire));
    EXPECT_EQ(results[0], 11);
    EXPECT_EQ(results[1], 22);
}

} // namespace mocking

namespace mocking {

void concurrency_late_mutation_rejected_after_runtime_start() {
    gentest::mock<Calculator> mock_calc;
    std::mutex                gate_mtx;
    std::condition_variable   gate_cv;
    bool                      first_entered = false;
    bool                      first_release = false;
    std::array<int, 2>        results{0, 0};
    std::atomic<bool>         first_args_ok{false};

    EXPECT_CALL(mock_calc, compute).times(1).with(1, 1).invokes([&](int lhs, int rhs) {
        first_args_ok.store(lhs == 1 && rhs == 1, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(gate_mtx);
            first_entered = true;
        }
        gate_cv.notify_all();
        std::unique_lock<std::mutex> lk(gate_mtx);
        gate_cv.wait(lk, [&] { return first_release; });
        return 11;
    });
    auto second = EXPECT_CALL(mock_calc, compute);
    second.times(1).with(1, 1).returns(22);

    auto        context = gentest::get_current_context();
    Calculator *iface   = &mock_calc;
    std::thread t1([&] {
        auto guard = gentest::set_current_context(context);
        results[0] = iface->compute(1, 1);
    });

    {
        std::unique_lock<std::mutex> lk(gate_mtx);
        gate_cv.wait(lk, [&] { return first_entered; });
    }

    // Rejected late configuration must report a failure without changing dispatch.
    const auto owner      = gentest::detail::current_test();
    const auto diagnostic = std::make_shared<gentest::detail::TestContextInfo>();
    gentest::detail::start_context(*diagnostic);
    gentest::detail::set_current_test(diagnostic, gentest::detail::CurrentContextRole::Owner);
    second.returns(99);
    gentest::detail::set_current_test(owner, gentest::detail::CurrentContextRole::Owner);

    std::thread t2([&] {
        auto guard = gentest::set_current_context(context);
        results[1] = iface->compute(1, 1);
    });

    {
        std::lock_guard<std::mutex> lk(gate_mtx);
        first_release = true;
    }
    gate_cv.notify_all();

    t1.join();
    t2.join();

    EXPECT_EQ(results[0], 11);
    EXPECT_EQ(results[1], 22);
    EXPECT_TRUE(first_args_ok.load(std::memory_order_acquire));
    ASSERT_EQ(diagnostic->failures.size(), std::size_t{1});
    EXPECT_TRUE(diagnostic->failures.front().find("Cannot configure mock") != std::string::npos);
}

} // namespace mocking

namespace mocking {

void nice_unexpected_ok() {
    gentest::mock<Ticker> mock_tick;
    gentest::make_nice(mock_tick, true);
    // No expectations set; unexpected call should be tolerated in nice mode
    mock_tick.tick(123);
    EXPECT_TRUE(true);
}

} // namespace mocking
