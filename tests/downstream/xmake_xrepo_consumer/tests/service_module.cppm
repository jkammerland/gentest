export module downstream.xrepo.service;

export namespace downstream {

struct Service {
    virtual ~Service()           = default;
    virtual int compute(int arg) = 0;
};

} // namespace downstream
