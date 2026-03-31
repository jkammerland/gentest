module;

export module gentest.header_unit_import_preamble_provider;

export namespace header_unit_proof {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace header_unit_proof
