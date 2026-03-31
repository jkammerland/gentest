module;

export module gentest.multi_imported_sibling_provider_beta;

export namespace multi_imported_sibling::beta {

struct Service {
    virtual ~Service()                = default;
    virtual int compute_beta(int arg) = 0;
};

} // namespace multi_imported_sibling::beta
