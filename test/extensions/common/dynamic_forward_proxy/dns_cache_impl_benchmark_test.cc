#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "common/config/utility.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache_impl.h"
#include "extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {
namespace {

using namespace ::testing;

struct DnsCacheTester {
  void initialize(uint64_t num_hosts) {
    config_.set_name("foo");
    config_.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V4_ONLY);
    config_.mutable_max_hosts()->set_value(num_hosts);
    EXPECT_CALL(dispatcher_, createDnsResolver(_, _)).WillOnce(Return(resolver_));
    dns_cache_ =
        std::make_unique<DnsCacheImpl>(dispatcher_, tls_, random_, loader_, store_, config_);
    update_callbacks_handle_ = dns_cache_->addUpdateCallbacks(update_callbacks_);
  }

  void doResolution(const std::string& hostname) {
    NiceMock<MockLoadDnsCacheEntryCallbacks> callbacks;
    Network::DnsResolver::ResolveCb resolve_cb;
    EXPECT_CALL(*resolver_, resolve(hostname, _, _))
        .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));
    auto result = dns_cache_->loadDnsCacheEntry(hostname, 80, callbacks);
    resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
               TestUtility::makeDnsResponse({"10.0.0.1"}));
  }

  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> resolver_{
      std::make_shared<NiceMock<Network::MockDnsResolver>>()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> loader_;
  Stats::IsolatedStoreImpl store_;
  std::unique_ptr<DnsCache> dns_cache_;
  NiceMock<MockUpdateCallbacks> update_callbacks_;
  DnsCache::AddUpdateCallbacksHandlePtr update_callbacks_handle_;
};

void benchmarkDnsCacheInsertion(::benchmark::State& state) {
  const uint64_t initial_items = state.range(0);
  const std::string hostname_to_insert = "not-in-cache.com";

  for (auto _ : state) {
    state.PauseTiming();
    DnsCacheTester tester;
    tester.initialize(initial_items + 1);
    for (uint64_t i = 0; i < initial_items; ++i) {
      const auto hostname = fmt::format("{}.com", i);
      tester.doResolution(hostname);
    }
    state.ResumeTiming();
    tester.doResolution(hostname_to_insert);
    state.SetComplexityN(initial_items);
  }
}

BENCHMARK(benchmarkDnsCacheInsertion)
    ->RangeMultiplier(10)
    ->Range(10, 10000)
    ->Complexity(::benchmark::oN)
    ->Unit(::benchmark::kMillisecond);

void benchmarkFlatHashMapCopy(::benchmark::State& state) {
  const uint64_t initial_items = state.range(0);

  for (auto _ : state) {
    state.PauseTiming();
    absl::flat_hash_map<std::string, std::shared_ptr<int>> test_map;

    for (uint64_t i = 0; i < initial_items; ++i) {
      std::string hostname = fmt::format("{}.com", i);
      test_map.try_emplace("foo-" + hostname, std::make_shared<int>(i));
    }

    state.ResumeTiming();
    absl::flat_hash_map<std::string, std::shared_ptr<int>> my_other_map{test_map};
    ::benchmark::DoNotOptimize(test_map.empty());

    state.SetComplexityN(initial_items);
  }
}

BENCHMARK(benchmarkFlatHashMapCopy)
    ->RangeMultiplier(10)
    ->Range(10, 10000000)
    ->Complexity(::benchmark::oN)
    ->Unit(::benchmark::kMillisecond);

} // namespace
} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
