# Мини-тест

Учебное веб-приложение по сетевой безопасности. В редакторе можно менять вопросы, а на экране теста — отправлять ответы и получать балл. API и воркер написаны на C++20.

Nginx передаёт запросы в API. PostgreSQL хранит вопросы и попытки, Redis кэширует список вопросов для прохождения, а NATS JetStream передаёт ответы воркеру. В Docker Compose два узла PostgreSQL и три узла NATS. Сети `web` и `data` разделены, разрешённые соединения контролирует `nftables`.

## Запуск

Выполняй команды из корня проекта на Linux или в Linux-ВМ с Docker Engine, Docker Compose, `nftables` и systemd.

```sh
sudo nft -c -f infra/mini-test.nft
sudo install -m 0644 infra/mini-test.nft /etc/mini-test.nft
sudo install -m 0644 infra/mini-test-firewall.service /etc/systemd/system/mini-test-firewall.service
sudo systemctl daemon-reload
sudo systemctl enable --now mini-test-firewall.service
docker compose up -d --build --wait
```

- Редактор вопросов — `http://127.0.0.1:8080/`.
- Прохождение теста — `http://127.0.0.1:8080/quiz.html`.
- Проверка API — `curl -fsS http://127.0.0.1:8080/api/hello`.

Команды для переключения PostgreSQL, проверки кластера JetStream и восстановления дампа собраны в [инструкции по демонстрации](infra/ha/README.md).
