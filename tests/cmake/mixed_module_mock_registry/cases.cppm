module;

export module gentest.mixed_module_cases;

export namespace mixmod {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace mixmod
