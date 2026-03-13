module;

export module gentest.imported_sibling_provider;

export namespace imported_sibling::provider {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace imported_sibling::provider
