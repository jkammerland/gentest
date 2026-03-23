module;

export module gentest.mixed_module_same_block_cases;

export namespace sameblock {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace sameblock
