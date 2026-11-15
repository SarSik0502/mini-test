#include "database.hpp"
#include "jetstream.hpp"

#include <hiredis/hiredis.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <unicode/uchar.h>
#include <unicode/utf8.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Json = nlohmann::json;
using RedisConnection = std::unique_ptr<redisContext, decltype(&redisFree)>;
using RedisReply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

struct ApiError {
    int status;
    Json detail;
};

struct Question {
    std::string prompt;
    std::array<std::string, 3> options;
    std::string correct_option;
};

[[noreturn]] void InvalidInput(Json location, std::string_view message) {
    throw ApiError{422, Json::array({{{"loc", location}, {"msg", message}}})};
}

std::string ReadText(const Json& value, int32_t max_length, const Json& location) {
    if (!value.is_string()) {
        InvalidInput(location, "Input should be a valid string");
    }

    const auto& text = value.get_ref<const std::string&>();
    const auto size = static_cast<int32_t>(text.size());
    int32_t start = -1;
    int32_t end = 0;
    int32_t length = 0;
    int32_t pending_spaces = 0;
    for (int32_t offset = 0; offset < size;) {
        const auto position = offset;
        UChar32 character;
        U8_NEXT(text.data(), offset, size, character);
        if (character <= 0) {
            InvalidInput(location, "Input contains an invalid character");
        }
        if (u_isUWhiteSpace(character)) {
            ++pending_spaces;
            continue;
        }
        if (start == -1) {
            start = position;
            pending_spaces = 0;
        }
        length += 1 + pending_spaces;
        pending_spaces = 0;
        end = offset;
    }

    if (length == 0 || length > max_length) {
        InvalidInput(location, "Input has an invalid length");
    }
    return text.substr(start, end - start);
}

Question ReadQuestion(const std::string& body) {
    const auto payload = Json::parse(body, nullptr, false);
    if (!payload.is_object()) {
        InvalidInput(Json::array({"body"}), "Input should be a valid JSON object");
    }

    Question question;
    question.prompt = ReadText(
        payload.value("prompt", Json{}), 500, Json::array({"body", "prompt"})
    );

    const auto options = payload.value("options", Json{});
    if (!options.is_array() || options.size() != question.options.size()) {
        InvalidInput(Json::array({"body", "options"}), "Exactly three options are required");
    }
    for (std::size_t index = 0; index < question.options.size(); ++index) {
        question.options[index] = ReadText(options[index], 200, Json::array({"body", "options", index}));
    }

    const auto correct = payload.value("correct_option", Json{});
    if (!correct.is_number_integer() || correct < 1 || correct > 3) {
        InvalidInput(Json::array({"body", "correct_option"}), "Input should be an integer from 1 to 3");
    }
    question.correct_option = correct.dump();
    return question;
}

std::string ReadQuestionId(const httplib::Request& request) {
    const auto id = request.matches[1].str();
    int32_t number = 0;
    const auto end = id.data() + id.size();
    const auto parsed = std::from_chars(id.data(), end, number);
    if (parsed.ec != std::errc{} || parsed.ptr != end || number <= 0) {
        InvalidInput(Json::array({"path", "question_id"}), "Input should be a positive 32-bit integer");
    }
    return id;
}

void RespondJson(httplib::Response& response, int status, const std::string& body) {
    response.status = status;
    response.set_content(body, "application/json");
}

void RequireQuestion(const Result& result) {
    if (PQntuples(result.get()) == 0) {
        throw ApiError{404, "Вопрос не найден"};
    }
}

Json ReadQuizSnapshot() {
    const auto result = Query(
        "SELECT json_build_object("
        "'version', md5(COALESCE(string_agg(id::text || ':' || xmin::text, ',' ORDER BY id), '')), "
        "'questions', COALESCE(json_agg(json_build_object("
        "'id', id, 'prompt', prompt, 'options', options) ORDER BY id), '[]'::json), "
        "'key', COALESCE(json_object_agg(id, correct_option), '{}'::json)) FROM questions"
    );
    return Json::parse(PQgetvalue(result.get(), 0, 0));
}

void RespondQuiz(httplib::Response& response, const Json& quiz, const char* cache_status) {
    response.set_header("X-Quiz-Version", quiz.at("version").get_ref<const std::string&>());
    response.set_header("X-Cache", cache_status);
    RespondJson(response, 200, quiz.at("questions").dump());
}

RedisConnection ConnectRedis() {
    const auto* host = std::getenv("REDIS_HOST");
    if (host == nullptr || *host == '\0') {
        return {nullptr, redisFree};
    }

    constexpr timeval timeout{0, 250'000};
    RedisConnection connection(redisConnectWithTimeout(host, 6379, timeout), redisFree);
    if (!connection || connection->err || redisSetTimeout(connection.get(), timeout) != REDIS_OK) {
        connection.reset();
    }
    return connection;
}

class QuizCache {
public:
    void Read(httplib::Response& response) {
        const std::lock_guard lock(mutex_);
        auto redis = ConnectRedis();
        response.set_header("Cache-Control", "no-store");

        if (can_read_cache_ && redis) {
            const RedisReply cached(
                static_cast<redisReply*>(redisCommand(redis.get(), "GET %s", key_)), freeReplyObject
            );
            if (cached && cached->type == REDIS_REPLY_STRING) {
                RespondQuiz(response, Json::parse(cached->str, cached->str + cached->len), "HIT");
                return;
            }
        }

        auto quiz = ReadQuizSnapshot();
        quiz.erase("key");
        const auto body = quiz.dump();
        can_read_cache_ = false;
        if (redis) {
            const RedisReply stored(
                static_cast<redisReply*>(
                    redisCommand(redis.get(), "SET %s %b EX %d", key_, body.data(), body.size(), ttl_seconds_)
                ),
                freeReplyObject
            );
            can_read_cache_ = stored && stored->type == REDIS_REPLY_STATUS &&
                std::string_view(stored->str, stored->len) == "OK";
        }
        RespondQuiz(response, quiz, can_read_cache_ ? "MISS" : "BYPASS");
    }

    Result WriteQuestion(const char* sql, std::initializer_list<const char*> parameters) {
        const std::lock_guard lock(mutex_);
        const bool cache_was_readable = can_read_cache_;
        can_read_cache_ = false;
        auto result = Query(sql, parameters);
        if (PQntuples(result.get()) == 0) {
            can_read_cache_ = cache_was_readable;
            return result;
        }
        if (auto redis = ConnectRedis()) {
            const RedisReply removed(
                static_cast<redisReply*>(redisCommand(redis.get(), "DEL %s", key_)), freeReplyObject
            );
        }
        return result;
    }

private:
    static constexpr char key_[] = "mini-test:quiz:v2";
    static constexpr int ttl_seconds_ = 60;
    // Serialize writes and cache fills within the single API process.
    std::mutex mutex_;
    // After a write or restart, skip GET until this process has stored a fresh value.
    bool can_read_cache_ = false;
};

bool IsAttemptId(const std::string& id) {
    static const std::regex pattern(
        "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"
    );
    return std::regex_match(id, pattern);
}

Result FindAttemptJob(const std::string& id) {
    return Query("SELECT job, score IS NOT NULL FROM attempts WHERE id = $1", {id.c_str()});
}

void SubmitAttempt(const httplib::Request& request, httplib::Response& response) {
    const auto payload = Json::parse(request.body, nullptr, false);
    if (!payload.is_object() || !payload.contains("id") || !payload["id"].is_string() ||
        !IsAttemptId(payload["id"].get_ref<const std::string&>()) ||
        !payload.contains("version") || !payload["version"].is_string() ||
        !payload.contains("answers") || !payload["answers"].is_object() || payload["answers"].empty()) {
        throw ApiError{422, "Укажите идентификатор попытки, версию теста и ответы"};
    }

    const auto& id = payload["id"].get_ref<const std::string&>();
    const auto& version = payload["version"];
    const auto& answers = payload["answers"];
    for (const auto& answer : answers) {
        if (!answer.is_number_integer() || answer < 1 || answer > 3) {
            throw ApiError{422, "Каждый ответ должен быть номером варианта от 1 до 3"};
        }
    }

    auto attempt = FindAttemptJob(id);
    if (PQntuples(attempt.get()) == 0) {
        const auto quiz = ReadQuizSnapshot();
        if (version != quiz["version"]) {
            throw ApiError{409, "Вопросы изменились. Загрузите тест заново"};
        }
        const auto& key = quiz["key"];
        if (answers.size() != key.size()) {
            throw ApiError{422, "Ответьте на все вопросы теста"};
        }
        for (const auto& answer : answers.items()) {
            if (!key.contains(answer.key())) {
                throw ApiError{422, "Ответы содержат неизвестный вопрос"};
            }
        }

        const auto job = Json{{"id", id}, {"version", version}, {"answers", answers}, {"key", key}}.dump();
        const auto total = std::to_string(key.size());
        attempt = Query(
            "INSERT INTO attempts (id, job, total) VALUES ($1, $2, $3) "
            "ON CONFLICT (id) DO NOTHING RETURNING job, score IS NOT NULL",
            {id.c_str(), job.c_str(), total.c_str()}
        );
        if (PQntuples(attempt.get()) == 0) {
            attempt = FindAttemptJob(id);
        }
    }

    const std::string job = PQgetvalue(attempt.get(), 0, 0);
    const auto saved = Json::parse(job);
    if (saved["answers"] != answers || saved["version"] != version) {
        throw ApiError{409, "Эта попытка уже содержит другие ответы"};
    }
    if (std::string_view(PQgetvalue(attempt.get(), 0, 1)) == "f") {
        const auto sequence = JetStream().Publish(id, job);
        std::cout << "Published attempt " << id << " to JetStream, sequence " << sequence << std::endl;
    }
    RespondJson(response, 202, Json{{"id", id}}.dump());
}

void ReadAttempt(const httplib::Request& request, httplib::Response& response) {
    const auto id = request.matches[1].str();
    if (!IsAttemptId(id)) {
        throw ApiError{422, "Некорректный идентификатор попытки"};
    }
    const auto result = Query(
        "SELECT json_build_object('id', id, 'score', score, 'total', total, "
        "'status', CASE WHEN score IS NULL THEN 'pending' ELSE 'completed' END) "
        "FROM attempts WHERE id = $1",
        {id.c_str()}
    );
    if (PQntuples(result.get()) == 0) {
        throw ApiError{404, "Попытка не найдена"};
    }
    response.set_header("Cache-Control", "no-store");
    RespondJson(response, 200, PQgetvalue(result.get(), 0, 0));
}

}  // namespace

int main() {
    QuizCache quiz_cache;
    httplib::Server server;
    server.set_payload_max_length(1024 * 1024);
    server.set_exception_handler([](
        const httplib::Request&, httplib::Response& response, std::exception_ptr exception
    ) {
        try {
            std::rethrow_exception(exception);
        } catch (const ApiError& error) {
            RespondJson(response, error.status, Json{{"detail", error.detail}}.dump());
        } catch (const DatabaseUnavailable&) {
            RespondJson(response, 503, Json{{"detail", "База данных временно недоступна"}}.dump());
        } catch (const BrokerError& error) {
            std::cerr << error.what() << '\n';
            RespondJson(response, 503, Json{{"detail", "Очередь временно недоступна. Повторите отправку"}}.dump());
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            RespondJson(response, 500, Json{{"detail", "Внутренняя ошибка сервера"}}.dump());
        }
    });

    server.Get("/api/hello", [](const httplib::Request&, httplib::Response& response) {
        response.set_content("hello-world", "text/plain; charset=utf-8");
    });

    server.Get("/api/questions", [](const httplib::Request&, httplib::Response& response) {
        const auto result = Query(
            "SELECT COALESCE(json_agg(q ORDER BY q.id), '[]'::json) FROM questions AS q"
        );
        RespondJson(response, 200, PQgetvalue(result.get(), 0, 0));
    });

    server.Get("/api/quiz", [&quiz_cache](const httplib::Request&, httplib::Response& response) {
        quiz_cache.Read(response);
    });
    server.Post("/api/attempts", SubmitAttempt);
    server.Get(R"(/api/attempts/([^/]+))", ReadAttempt);

    server.Post("/api/questions", [&quiz_cache](const httplib::Request& request, httplib::Response& response) {
        const auto question = ReadQuestion(request.body);
        const auto& [prompt, options, correct] = question;
        const auto result = quiz_cache.WriteQuestion(
            "INSERT INTO questions (prompt, options, correct_option) "
            "VALUES ($1, ARRAY[$2, $3, $4]::text[], $5) RETURNING row_to_json(questions)",
            {prompt.c_str(), options[0].c_str(), options[1].c_str(), options[2].c_str(), correct.c_str()}
        );
        RespondJson(response, 201, PQgetvalue(result.get(), 0, 0));
    });

    const auto question_path = R"(/api/questions/([^/]+))";
    server.Put(question_path, [&quiz_cache](const httplib::Request& request, httplib::Response& response) {
        const auto id = ReadQuestionId(request);
        const auto question = ReadQuestion(request.body);
        const auto& [prompt, options, correct] = question;
        const auto result = quiz_cache.WriteQuestion(
            "UPDATE questions SET prompt = $1, options = ARRAY[$2, $3, $4]::text[], correct_option = $5 "
            "WHERE id = $6 RETURNING row_to_json(questions)",
            {prompt.c_str(), options[0].c_str(), options[1].c_str(), options[2].c_str(), correct.c_str(), id.c_str()}
        );
        RequireQuestion(result);
        RespondJson(response, 200, PQgetvalue(result.get(), 0, 0));
    });

    server.Delete(question_path, [&quiz_cache](const httplib::Request& request, httplib::Response& response) {
        const auto id = ReadQuestionId(request);
        const auto result = quiz_cache.WriteQuestion(
            "DELETE FROM questions WHERE id = $1 RETURNING id", {id.c_str()}
        );
        RequireQuestion(result);
        response.status = 204;
    });

    if (!server.listen("0.0.0.0", 8000)) {
        std::cerr << "Failed to start the API on port 8000\n";
        return 1;
    }
}
