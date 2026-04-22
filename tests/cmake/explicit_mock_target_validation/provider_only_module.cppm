export module fixture.validation.provider_only_module;

export namespace fixture::validation {

struct ProviderOnlyService {
    virtual ~ProviderOnlyService() = default;
    virtual int value()            = 0;
};

} // namespace fixture::validation
