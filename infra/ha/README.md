# Демонстрация отказоустойчивости «Мини-теста»

Команды выполняются в ВМ `mini-test` из каталога проекта. При изменении сетевых правил установи `infra/mini-test.nft` в `/etc/mini-test.nft` и перезагрузи `mini-test-firewall.service`. Стенд запускается командой `docker compose up -d --build --wait`.

## PostgreSQL

Проверь роли обоих узлов. Значение `f` означает primary, `t` означает standby.

```sh
for node in postgres postgres-standby; do
  printf '%s ' "$node"
  docker compose exec -T "$node" psql -XAt -U postgres -d mini_test \
    -c 'SELECT pg_is_in_recovery()'
done
```

Задай `primary` и `standby` по результату проверки. Перед отказом дождись статуса `streaming` на primary и проверь, что контрольная попытка читается на standby.

```sh
docker compose exec -T "$primary" psql -XAt -U postgres -d mini_test \
  -c 'SELECT state FROM pg_stat_replication'
docker compose exec -T -u postgres "$primary" \
  touch /var/lib/postgresql/data/.mini-test-fenced &&
  docker compose kill -s SIGKILL "$primary" &&
  running=$(docker compose ps --status running --services "$primary") &&
  test -z "$running" &&
  docker compose exec -T "$standby" psql -XAt -U postgres -d mini_test \
    -c 'SELECT pg_promote(true, 30)'
```

Открой старый результат и пройди новый тест. API и воркер при новом подключении выбирают PostgreSQL, который принимает записи. Верни остановленный узел как реплику. Команда создаёт новую физическую копию в томе и сохраняет прежний каталог данных.

```sh
running=$(docker compose ps --status running --services "$primary") &&
  test -z "$running" &&
  docker compose run --rm --no-deps "$primary" reseed "$standby" &&
  docker compose up -d --no-deps --wait "$primary"
docker compose exec -T "$primary" psql -XAt -U postgres -d mini_test \
  -c 'SELECT pg_is_in_recovery(), status FROM pg_stat_wal_receiver'
```

## NATS JetStream

Найди поток `QUIZ_ATTEMPTS` и поле `cluster.leader` в ответе мониторинга. Запрос можно выполнить на любом работающем NATS-узле.

```sh
docker compose exec -T nats wget -qO- \
  'http://127.0.0.1:8222/jsz?streams=1&consumers=1&config=1'
```

Останови найденного лидера командой `docker compose kill -s SIGKILL "$leader"`. Повтори запрос на одном из оставшихся узлов, найди нового лидера и пройди тест. Верни остановленный узел командой `docker compose up -d --no-deps --wait "$leader"`.

## Дамп и восстановление

Задай `primary` по текущим ролям, а `restore_db` укажи как имя новой пустой базы.

```sh
umask 077
dump=$(mktemp /tmp/mini-test.XXXXXX)
docker compose exec -T "$primary" pg_dump -U postgres -d mini_test --format=custom > "$dump"
docker compose exec -T "$primary" createdb -U postgres "$restore_db" &&
  docker compose exec -T "$primary" pg_restore -U postgres -d "$restore_db" \
    --exit-on-error --single-transaction < "$dump"
docker compose exec -T "$primary" psql -XAt -U postgres -d "$restore_db" \
  -c 'SELECT count(*) FROM questions' \
  -c 'SELECT id, score, total FROM attempts ORDER BY id'
```

Измеряй RTO от отключения узла до первой успешной записи результата. На проверке в ВМ переключение PostgreSQL заняло 1,41 с, а обработка попытки после отключения лидера JetStream заняла 7,37 с. Создание отдельной базы и восстановление небольшого дампа заняли 0,33 с.

Для демонстрации цель RTO до 60 с оставляет время на ручное повышение PostgreSQL и повторную проверку. Потоковая репликация асинхронная. Перед демонстрацией проверь, что контрольная запись дошла до standby. Восстановление из логического дампа возвращает данные на момент его снимка.

Для учебного стенда цель SLO составляет 99% принятых попыток с результатом в течение 30 с за сутки. Один прогон подтверждает сценарии переключения. Для оценки SLO, среднего времени восстановления (MTTR) и среднего времени между отказами (MTBF) нужны наблюдения за несколько периодов. Все контейнеры работают в одной ВМ.
