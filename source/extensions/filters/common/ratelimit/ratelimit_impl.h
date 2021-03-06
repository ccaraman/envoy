#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/service/ratelimit/v2/rls.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/common/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

typedef Grpc::TypedAsyncRequestCallbacks<envoy::service::ratelimit::v2::RateLimitResponse>
    RateLimitAsyncCallbacks;

struct ConstantValues {
  const std::string TraceStatus = "ratelimit_status";
  const std::string TraceOverLimit = "over_limit";
  const std::string TraceOk = "ok";
};

typedef ConstSingleton<ConstantValues> Constants;

// TODO(htuch): We should have only one client per thread, but today we create one per filter stack.
// This will require support for more than one outstanding request per client (limit() assumes only
// one today).
class GrpcClientImpl : public Client,
                       public RateLimitAsyncCallbacks,
                       public Logger::Loggable<Logger::Id::config> {
public:
  GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                 const absl::optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl();

  static void createRequest(envoy::service::ratelimit::v2::RateLimitRequest& request,
                            const std::string& domain,
                            const std::vector<Envoy::RateLimit::Descriptor>& descriptors);

  // Filters::Common::RateLimit::Client
  void cancel() override;
  void limit(RequestCallbacks& callbacks, const std::string& domain,
             const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
             Tracing::Span& parent_span) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap&) override {}
  void onSuccess(std::unique_ptr<envoy::service::ratelimit::v2::RateLimitResponse>&& response,
                 Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  const Protobuf::MethodDescriptor& service_method_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncRequest* request_{};
  absl::optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
};

class GrpcFactoryImpl : public ClientFactory {
public:
  GrpcFactoryImpl(const envoy::config::ratelimit::v2::RateLimitServiceConfig& config,
                  Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope);

  // Filters::Common::RateLimit::ClientFactory
  ClientPtr create(const absl::optional<std::chrono::milliseconds>& timeout) override;

  const absl::optional<envoy::config::ratelimit::v2::RateLimitServiceConfig>&
  rateLimitConfig() const override {
    return config_;
  }

private:
  Grpc::AsyncClientFactoryPtr async_client_factory_;
  const absl::optional<envoy::config::ratelimit::v2::RateLimitServiceConfig> config_;
};

// TODO(ramaraochavali): NullClientImpl and NullFactoryImpl should be removed when we remove rate
// limit config from bootstrap.
class NullClientImpl : public Client {
public:
  // Filters::Common::RateLimit::Client
  void cancel() override {}
  void limit(RequestCallbacks& callbacks, const std::string&,
             const std::vector<Envoy::RateLimit::Descriptor>&, Tracing::Span&) override {
    callbacks.complete(LimitStatus::OK, nullptr);
  }
};

class NullFactoryImpl : public ClientFactory {
public:
  // Filters::Common::RateLimit::ClientFactory
  ClientPtr create(const absl::optional<std::chrono::milliseconds>&) override {
    return ClientPtr{new NullClientImpl()};
  }

  const absl::optional<envoy::config::ratelimit::v2::RateLimitServiceConfig>&
  rateLimitConfig() const override {
    return config_;
  }

private:
  const absl::optional<envoy::config::ratelimit::v2::RateLimitServiceConfig> config_;
};

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
