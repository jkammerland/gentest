export module fixture.validation.partition_module:part;

export import gentest.mock;

export namespace fixture::validation {

struct PartitionService {
    virtual ~PartitionService() = default;
    virtual int value()         = 0;
};

using PartitionServiceMock = gentest::mock<PartitionService>;

} // namespace fixture::validation
