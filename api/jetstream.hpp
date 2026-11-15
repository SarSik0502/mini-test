#pragma once

#include <nats/nats.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

struct BrokerError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using Subscription = std::unique_ptr<natsSubscription, decltype(&natsSubscription_Destroy)>;

void CheckNats(natsStatus status, const char* operation, jsErrCode error_code = {});

class JetStream {
public:
    JetStream();
    uint64_t Publish(const std::string& id, const std::string& body);
    Subscription Subscribe();

private:
    std::unique_ptr<natsConnection, decltype(&natsConnection_Destroy)> connection_{nullptr, natsConnection_Destroy};
    std::unique_ptr<jsCtx, decltype(&jsCtx_Destroy)> stream_{nullptr, jsCtx_Destroy};
};
