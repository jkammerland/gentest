module;

export module gentest.multiline_provider;

export namespace multiline::provider {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace multiline::provider
