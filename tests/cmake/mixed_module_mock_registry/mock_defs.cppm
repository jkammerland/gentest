export module gentest.mixed_module_mock_defs;

export import gentest.mock;

export import gentest.mixed_module_cases;
export import gentest.mixed_module_extra_cases;
export import gentest.mixed_module_same_block_cases;

export namespace mixmod::mocks {

using ServiceMock = gentest::mock<mixmod::Service>;

} // namespace mixmod::mocks

export namespace extramod::mocks {

using WorkerMock = gentest::mock<extramod::Worker>;

} // namespace extramod::mocks

export namespace sameblock::mocks {

using ServiceMock = gentest::mock<sameblock::Service>;

} // namespace sameblock::mocks
