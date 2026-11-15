#include "jetstream.hpp"

#include <cstdlib>
#include <sstream>
#include <vector>

namespace {

constexpr char stream_name[] = "QUIZ_ATTEMPTS";
constexpr char subject[] = "quiz.attempts.submitted";
constexpr char consumer_name[] = "quiz-worker";
constexpr int replicas = 3;

}  // namespace

void CheckNats(natsStatus status, const char* operation, jsErrCode error_code) {
    if (status != NATS_OK) {
        throw BrokerError(std::string(operation) + ": " + natsStatus_GetText(status) +
                          " (JetStream: " + std::to_string(error_code) + ")");
    }
}

JetStream::JetStream() {
    const char* url = std::getenv("NATS_URL");
    if (url == nullptr) {
        throw BrokerError("NATS_URL is missing");
    }

    natsOptions* configuration = nullptr;
    CheckNats(natsOptions_Create(&configuration), "Create NATS options");
    const std::unique_ptr<natsOptions, decltype(&natsOptions_Destroy)> options(configuration, natsOptions_Destroy);
    std::istringstream urls(url);
    std::vector<std::string> addresses;
    for (std::string address; std::getline(urls, address, ',');) {
        addresses.push_back(address);
    }
    std::vector<const char*> servers;
    for (const auto& address : addresses) {
        servers.push_back(address.c_str());
    }
    CheckNats(natsOptions_SetServers(options.get(), servers.data(), static_cast<int>(servers.size())), "Set NATS servers");
    CheckNats(natsOptions_SetTimeout(options.get(), 1500), "Set NATS timeout");

    natsConnection* connection = nullptr;
    const auto connected = natsConnection_Connect(&connection, options.get());
    connection_.reset(connection);
    CheckNats(connected, "Connect to NATS");

    jsOptions js_options;
    jsOptions_Init(&js_options);
    js_options.Wait = 1500;
    jsCtx* context = nullptr;
    const auto opened = natsConnection_JetStream(&context, connection_.get(), &js_options);
    stream_.reset(context);
    CheckNats(opened, "Open JetStream context");

    jsStreamInfo* info = nullptr;
    jsErrCode error_code{};
    const auto found = js_GetStreamInfo(&info, stream_.get(), stream_name, nullptr, &error_code);
    const std::unique_ptr<jsStreamInfo, decltype(&jsStreamInfo_Destroy)> current(info, jsStreamInfo_Destroy);
    if (found == NATS_NOT_FOUND) {
        const char* subjects[] = {subject};
        jsStreamConfig config;
        jsStreamConfig_Init(&config);
        config.Name = stream_name;
        config.Subjects = subjects;
        config.SubjectsLen = 1;
        config.Storage = js_FileStorage;
        config.Retention = js_WorkQueuePolicy;
        config.Replicas = replicas;
        const auto created = js_AddStream(nullptr, stream_.get(), &config, nullptr, &error_code);
        CheckNats(created, "Create attempt stream", error_code);
        return;
    }
    CheckNats(found, "Read attempt stream", error_code);
    if (current->Config->Replicas != replicas) {
        current->Config->Replicas = replicas;
        const auto updated = js_UpdateStream(nullptr, stream_.get(), current->Config, nullptr, &error_code);
        CheckNats(updated, "Set attempt stream replica count", error_code);
    }
}

uint64_t JetStream::Publish(const std::string& id, const std::string& body) {
    jsPubOptions options;
    jsPubOptions_Init(&options);
    options.MsgId = id.c_str();
    options.ExpectStream = stream_name;
    jsPubAck* ack = nullptr;
    jsErrCode error_code{};
    const auto published = js_Publish(
        &ack, stream_.get(), subject, body.data(), static_cast<int>(body.size()), &options, &error_code
    );
    const std::unique_ptr<jsPubAck, decltype(&jsPubAck_Destroy)> result(ack, jsPubAck_Destroy);
    CheckNats(published, "Publish attempt", error_code);
    return result->Sequence;
}

Subscription JetStream::Subscribe() {
    jsConsumerInfo* info = nullptr;
    jsErrCode error_code{};
    const auto found = js_GetConsumerInfo(&info, stream_.get(), stream_name, consumer_name, nullptr, &error_code);
    const std::unique_ptr<jsConsumerInfo, decltype(&jsConsumerInfo_Destroy)> current(info, jsConsumerInfo_Destroy);
    if (found == NATS_NOT_FOUND) {
        jsConsumerConfig config;
        jsConsumerConfig_Init(&config);
        config.Durable = consumer_name;
        config.FilterSubject = subject;
        config.AckPolicy = js_AckExplicit;
        const auto created = js_AddConsumer(nullptr, stream_.get(), stream_name, &config, nullptr, &error_code);
        CheckNats(created, "Create attempt consumer", error_code);
    } else {
        CheckNats(found, "Read attempt consumer", error_code);
    }

    jsSubOptions options;
    jsSubOptions_Init(&options);
    options.Stream = stream_name;
    options.Consumer = consumer_name;
    options.ManualAck = true;
    natsSubscription* subscription = nullptr;
    const auto subscribed = js_PullSubscribe(
        &subscription, stream_.get(), subject, consumer_name, nullptr, &options, &error_code
    );
    Subscription result(subscription, natsSubscription_Destroy);
    CheckNats(subscribed, "Bind attempt consumer", error_code);
    return result;
}
