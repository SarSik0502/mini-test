#pragma once

#include <libpq-fe.h>

#include <initializer_list>
#include <memory>
#include <stdexcept>

using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;

struct DatabaseUnavailable : std::runtime_error {
    DatabaseUnavailable() : std::runtime_error("PostgreSQL is unavailable") {}
};

Result Query(const char* sql, std::initializer_list<const char*> parameters = {});
