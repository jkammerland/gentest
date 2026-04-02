export module gentest.consumer_service;

export namespace consumer {

struct Service {
    virtual ~Service()           = default;
    virtual int compute(int arg) = 0;
};

} // namespace consumer
