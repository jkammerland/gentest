#include "public/fixture_header_mocks.hpp"
#include "public/fixture_alt_header_mocks.hpp"

int main() {
    fixture::mocks::ServiceMock alias_mock;
    gentest::expect(alias_mock, &fixture::Service::compute).times(1).with(3).returns(fixture::kServiceSentinel);
    fixture::Service *service = &alias_mock;
    if (service->compute(3) != fixture::kServiceSentinel) {
        return 1;
    }

    gentest::mock<fixture::Service> raw_mock;
    gentest::expect(raw_mock, &fixture::Service::compute).times(1).with(4).returns(7);
    fixture::Service *raw_service = &raw_mock;
    if (raw_service->compute(4) != 7) {
        return 2;
    }

    fixture::mocks::AltServiceMock alt_alias_mock;
    gentest::expect(alt_alias_mock, &fixture::AltService::scale).times(1).with(5).returns(15);
    fixture::AltService *alt_service = &alt_alias_mock;
    if (alt_service->scale(5) != 15) {
        return 3;
    }

    gentest::mock<fixture::AltService> alt_raw_mock;
    gentest::expect(alt_raw_mock, &fixture::AltService::scale).times(1).with(2).returns(8);
    fixture::AltService *alt_raw_service = &alt_raw_mock;
    if (alt_raw_service->scale(2) != 8) {
        return 4;
    }

    fixture::mocks::AggregatedServiceMock aggregated_mock;
    gentest::expect(aggregated_mock, &fixture::Service::compute).times(1).with(12).returns(24);
    fixture::Service *aggregated_service = &aggregated_mock;
    if (aggregated_service->compute(12) != 24) {
        return 5;
    }

    fixture::mocks::QuoteTaggedServiceMock quote_alias_mock;
    gentest::expect(quote_alias_mock, &fixture::TaggedService<'\"'>::adjust).times(1).with(5).returns(11);
    fixture::TaggedService<'\"'> *quote_service = &quote_alias_mock;
    if (quote_service->adjust(5) != 11) {
        return 6;
    }

    gentest::mock<fixture::TaggedService<'\\'>> slash_raw_mock;
    gentest::expect(slash_raw_mock, &fixture::TaggedService<'\\'>::adjust).times(1).with(6).returns(13);
    fixture::TaggedService<'\\'> *slash_service = &slash_raw_mock;
    if (slash_service->adjust(6) != 13) {
        return 7;
    }

    return 0;
}
