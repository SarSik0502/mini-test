#include "database.hpp"

#include <string>
#include <string_view>

Result Query(const char* sql, std::initializer_list<const char*> parameters) {
    // libpq reads the PG* environment variables supplied by Docker Compose.
    const std::unique_ptr<PGconn, decltype(&PQfinish)> connection(PQconnectdb(""), PQfinish);
    if (!connection || PQstatus(connection.get()) != CONNECTION_OK) {
        throw DatabaseUnavailable();
    }

    Result result(PQexecParams(
        connection.get(), sql, static_cast<int>(parameters.size()),
        nullptr, parameters.begin(), nullptr, nullptr, 0
    ), PQclear);
    if (result && PQresultStatus(result.get()) == PGRES_TUPLES_OK) {
        return result;
    }

    const char* code = result ? PQresultErrorField(result.get(), PG_DIAG_SQLSTATE) : nullptr;
    const std::string_view state = code ? code : "";
    if (PQstatus(connection.get()) != CONNECTION_OK || state.starts_with("08") ||
        state == "57P01" || state == "57P02" || state == "57P03") {
        throw DatabaseUnavailable();
    }
    throw std::runtime_error("PostgreSQL query failed, SQLSTATE " + std::string(state));
}
