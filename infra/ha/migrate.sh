#!/bin/bash
set -euo pipefail

for ((attempt = 1; attempt <= 30; attempt++)); do
    if psql -XqAtw --command="SELECT 1" >/dev/null 2>&1; then
        exec psql -Xw --set=ON_ERROR_STOP=1 \
            --file=/ha/replication-role.sql --file=/migrations/001-attempts.sql
    fi
    sleep 1
done

echo "No writable PostgreSQL node for migrations" >&2
exit 1
