#include "database.hpp"
#include "jetstream.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

using Json = nlohmann::json;

void Grade(natsMsg* message) {
    const auto* data = natsMsg_GetData(message);
    const auto job = Json::parse(data, data + natsMsg_GetDataLength(message), nullptr, false);
    if (!job.is_object() || !job.contains("id") || !job["id"].is_string() ||
        !job.contains("answers") || !job["answers"].is_object() ||
        !job.contains("key") || !job["key"].is_object() || job["key"].empty()) {
        std::cerr << "Rejected malformed attempt job\n";
        CheckNats(natsMsg_Term(message, nullptr), "Reject malformed attempt");
        return;
    }

    const auto& id = job["id"].get_ref<const std::string&>();
    const auto& answers = job["answers"];
    const auto& key = job["key"];
    int score = 0;
    for (auto it = key.begin(); it != key.end(); ++it) {
        if (answers.contains(it.key()) && answers[it.key()] == it.value()) {
            ++score;
        }
    }

    const auto score_text = std::to_string(score);
    const auto payload = job.dump();
    const auto result = Query(
        "UPDATE attempts SET score = $1 WHERE id::text = $2 AND job = $3::jsonb "
        "AND score IS NULL RETURNING id",
        {score_text.c_str(), id.c_str(), payload.c_str()}
    );
    if (PQntuples(result.get()) != 0) {
        std::cout << "Graded attempt " << id << ": " << score << '/' << key.size() << std::endl;
    }
    CheckNats(natsMsg_AckSync(message, nullptr, nullptr), "Acknowledge attempt");
}

void Consume() {
    JetStream broker;
    auto subscription = broker.Subscribe();
    std::cout << "Waiting for quiz attempts" << std::endl;
    while (true) {
        natsMsgList messages{};
        const auto status = natsSubscription_Fetch(&messages, subscription.get(), 1, 1500, nullptr);
        const std::unique_ptr<natsMsgList, decltype(&natsMsgList_Destroy)> batch(&messages, natsMsgList_Destroy);
        if (status == NATS_TIMEOUT) {
            continue;
        }
        CheckNats(status, "Fetch attempt");
        for (int index = 0; index < messages.Count; ++index) {
            try {
                Grade(messages.Msgs[index]);
            } catch (const std::exception& error) {
                std::cerr << "Attempt will be retried: " << error.what() << '\n';
                natsMsg_NakWithDelay(messages.Msgs[index], 1000, nullptr);
            }
        }
    }
}

}  // namespace

int main() {
    while (true) {
        try {
            Consume();
        } catch (const BrokerError& error) {
            std::cerr << error.what() << '\n';
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}
