#!/bin/bash
set -euo pipefail

data_root=$PGDATA
fence="$data_root/.mini-test-fenced"
replica_pointer="$data_root/.mini-test-replica"
escaped_password=${REPLICATION_PASSWORD//\\/\\\\}
escaped_password=${escaped_password//:/\\:}
install -d -m 0755 /run/mini-test
(umask 077; printf '*:5432:replication:mini_test_repl:%s\n' "$escaped_password" > /run/mini-test/repl.pgpass)
chown postgres:postgres /run/mini-test/repl.pgpass

seed_replica() {
    local source=$1 connection bootstrap
    connection="host=$source user=mini_test_repl passfile=/run/mini-test/repl.pgpass connect_timeout=2"
    bootstrap=$(mktemp -d "$data_root/.replica.XXXXXX")
    chown postgres:postgres "$bootstrap"
    gosu postgres pg_basebackup --no-password --dbname="$connection" \
        --pgdata="$bootstrap" --write-recovery-conf --wal-method=stream
    # Switch files only after a complete backup. Keep the previous data directory.
    printf '%s\n' "${bootstrap##*/}" > "$replica_pointer.tmp"
    mv "$replica_pointer.tmp" "$replica_pointer"
}

if [[ "${1:-}" == reseed ]]; then
    test -f "$fence"
    case "${2:-}" in
        postgres|postgres-standby) seed_replica "$2" ;;
        *) echo "Specify the writable PostgreSQL service" >&2; exit 2 ;;
    esac
    rm -f "$fence"
    exit 0
fi

if [[ -e "$fence" ]]; then
    echo "PostgreSQL node is fenced until rejoined as a standby" >&2
    exit 1
fi

if [[ ! -f "$replica_pointer" && ! -s "$data_root/PG_VERSION" && -n "${BOOTSTRAP_FROM:-}" ]]; then
    install -d -m 0700 -o postgres -g postgres "$data_root"
    connection="host=$BOOTSTRAP_FROM user=mini_test_repl passfile=/run/mini-test/repl.pgpass connect_timeout=2 dbname=replication replication=true"
    for ((attempt = 1; attempt <= 30; attempt++)); do
        if gosu postgres psql -XqAtw --dbname="$connection" --command=IDENTIFY_SYSTEM >/dev/null 2>&1; then
            break
        fi
        sleep 1
    done
    seed_replica "$BOOTSTRAP_FROM"
fi

if [[ -f "$replica_pointer" ]]; then
    read -r replacement < "$replica_pointer"
    [[ "$replacement" =~ ^\.replica\.[[:alnum:]]+$ ]]
    export PGDATA="$data_root/$replacement"
fi

exec docker-entrypoint.sh "$@"
