module;

export module gentest.multi_imported_sibling_provider_alpha;

export namespace multi_imported_sibling::alpha {

struct Service {
    virtual ~Service()                 = default;
    virtual int compute_alpha(int arg) = 0;
};

} // namespace multi_imported_sibling::alpha
