export module module_file_names.service;

export namespace module_file_names {

struct Service {
    virtual ~Service()           = default;
    virtual int compute(int arg) = 0;
};

} // namespace module_file_names
