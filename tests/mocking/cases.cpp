#include "cases.hpp"

#include "gentest/detail/runtime_context.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <type_traits>
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
        callback.value()(::async::Job{.id = 2}, &handle);
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
    EXPECT_EQ(iface->table({::approx::Tolerance{}, ::detail::Kind::second}).at(2).value_or(::match::Pattern{.id = -1}).id, 3);
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

void interface_conditional_noexcept() {
    // The generated overrides reproduce each resolved exception specification (#266).
    // Checked at run time: the codegen scan of this file sees placeholder mocks.
    EXPECT_TRUE(noexcept(std::declval<gentest::mock<CondSink<true>> &>().put()));
    EXPECT_TRUE(noexcept(std::declval<const gentest::mock<CondSink<true>> &>().peek()));
    EXPECT_TRUE(noexcept(std::declval<gentest::mock<CondSink<true>> &&>().drain()));
    EXPECT_TRUE(noexcept(std::declval<const gentest::mock<CondSink<true>> &>().inspect()));
    EXPECT_TRUE(noexcept(std::declval<gentest::mock<CondSink<true>> &>().plain()));
    EXPECT_FALSE(noexcept(std::declval<gentest::mock<CondSink<true>> &>().throwing()));
    EXPECT_FALSE(noexcept(std::declval<gentest::mock<CondSink<false>> &>().put()));
    EXPECT_FALSE(noexcept(std::declval<const gentest::mock<CondSink<false>> &>().peek()));
    EXPECT_FALSE(noexcept(std::declval<gentest::mock<CondSink<false>> &&>().drain()));
    EXPECT_FALSE(noexcept(std::declval<const gentest::mock<CondSink<false>> &>().inspect()));
    EXPECT_TRUE(noexcept(std::declval<gentest::mock<CondSink<false>> &>().plain()));
    EXPECT_TRUE(noexcept(std::declval<gentest::mock<TraitSink<noexcept_policy::NoThrow>> &>().put(1)));
    EXPECT_TRUE(noexcept(std::declval<const gentest::mock<TraitSink<noexcept_policy::NoThrow>> &>().get()));
    EXPECT_FALSE(noexcept(std::declval<gentest::mock<TraitSink<noexcept_policy::Throw>> &>().put(1)));
    EXPECT_FALSE(noexcept(std::declval<const gentest::mock<TraitSink<noexcept_policy::Throw>> &>().get()));
    EXPECT_TRUE(noexcept(std::declval<const gentest::mock<ValueSource<int, true>> &>().get()));
    EXPECT_TRUE(noexcept(std::declval<gentest::mock<ValueSource<int, true>> &>().set(1)));
    EXPECT_FALSE(noexcept(std::declval<const gentest::mock<ValueSource<long, false>> &>().get()));
    EXPECT_FALSE(noexcept(std::declval<gentest::mock<ValueSource<long, false>> &>().set(1L)));
    EXPECT_TRUE(std::is_nothrow_constructible_v<gentest::mock<CondCtor<true>>, int>);
    EXPECT_TRUE(std::is_constructible_v<gentest::mock<CondCtor<false>>, int>);
    EXPECT_FALSE(std::is_nothrow_constructible_v<gentest::mock<CondCtor<false>>, int>);

    gentest::mock<CondSink<true>> sink;
    int                           puts = 0;
    EXPECT_CALL(sink, put).times(1).invokes([&]() { ++puts; });
    EXPECT_CALL(sink, peek).times(1).returns(3);
    EXPECT_CALL(sink, drain).times(1).returns(4);
    EXPECT_CALL(sink, inspect).times(1).returns(5);
    CondSink<true> &iface = sink;
    iface.put();
    EXPECT_EQ(puts, 1);
    EXPECT_EQ(iface.peek(), 3);
    EXPECT_EQ(static_cast<CondSink<true> &&>(iface).drain(), 4);
    EXPECT_EQ(iface.inspect(), 5);

    gentest::mock<TraitSink<noexcept_policy::NoThrow>> traits;
    EXPECT_CALL(traits, get).times(1).returns(6);
    EXPECT_EQ(static_cast<const TraitSink<noexcept_policy::NoThrow> &>(traits).get(), 6);

    gentest::mock<ValueSource<long, false>> source;
    EXPECT_CALL(source, get).times(1).returns(7L);
    EXPECT_EQ(static_cast<const ValueSource<long, false> &>(source).get(), 7L);

    gentest::mock<CondCtor<true>> ctor_mock{8};
    EXPECT_CALL(ctor_mock, ping).times(1).returns(9);
    EXPECT_EQ(ctor_mock.seed, 8);
    EXPECT_EQ(static_cast<CondCtor<true> &>(ctor_mock).ping(), 9);
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

void unnamed_template_param_member_expect() {
    gentest::mock<UnnamedTemplateParam> mock_tagged;
    int                                 calls = 0;

    gentest::expect(mock_tagged, &UnnamedTemplateParam::template tagged<char, int>).times(1).invokes([&](int value) { calls += value; });

    mock_tagged.template tagged<char>(1);

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

void bench_measured_call_verification_does_not_throw() {
    // Simulate measured call phases in diagnostic contexts so the owner test stays green.
    const auto owner        = gentest::detail::current_test();
    const auto every_call   = std::make_shared<gentest::detail::TestContextInfo>();
    const auto unwinding    = std::make_shared<gentest::detail::TestContextInfo>();
    bool       unwound      = false;
    const auto unmet_verify = [] {
        gentest::mock<Ticker> mock_tick;
        EXPECT_CALL(mock_tick, tick).times(1);
    };
    gentest::detail::start_context(*every_call);
    gentest::detail::start_context(*unwinding);
    {
        gentest::detail::BenchPhaseScope call_phase(gentest::detail::BenchPhase::Call);
        gentest::detail::set_current_test(every_call, gentest::detail::CurrentContextRole::Owner);
        // Destructor verification records instead of throwing, and a body that
        // fails on every iteration keeps only a bounded prefix of failures.
        for (int i = 0; i < 1000; ++i)
            unmet_verify();
#if GENTEST_EXCEPTIONS_ENABLED
        gentest::detail::set_current_test(unwinding, gentest::detail::CurrentContextRole::Owner);
        try {
            gentest::mock<Ticker> mock_tick;
            EXPECT_CALL(mock_tick, tick).times(1);
            gentest::require(false, "fatal failure unwinds an unmet mock");
        } catch (const gentest::assertion &) { unwound = true; }
#endif
        gentest::detail::set_current_test(owner, gentest::detail::CurrentContextRole::Owner);
    }

    ASSERT_EQ(every_call->failures.size(), std::size_t{8});
    EXPECT_TRUE(every_call->failures.front().find("tick but observed 0") != std::string::npos, every_call->failures.front());
    if constexpr (gentest::detail::exceptions_enabled) {
        EXPECT_TRUE(unwound);
        ASSERT_EQ(unwinding->failures.size(), std::size_t{2});
        EXPECT_TRUE(unwinding->failures[0].find("fatal failure unwinds an unmet mock") != std::string::npos);
        EXPECT_TRUE(unwinding->failures[1].find("but observed 0") != std::string::npos);
    }
}

void nice_expected_calls_ok() {
    gentest::mock<Ticker> mock_tick;
    gentest::make_nice(mock_tick, true);
    // Nice mode must not disturb calls that stay within the expectation.
    gentest::expect(mock_tick, &Ticker::tick).times(2);
    mock_tick.tick(1);
    mock_tick.tick(2);
}

} // namespace mocking
