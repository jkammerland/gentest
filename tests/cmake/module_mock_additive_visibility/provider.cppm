module;

export module gentest.additive_provider;

export namespace provider {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace provider
