module;

export module gentest.story035.mock_split_provider;

export namespace story035_mock_split {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace story035_mock_split
