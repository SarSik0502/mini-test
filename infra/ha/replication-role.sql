\getenv replication_password REPLICATION_PASSWORD

SELECT format('CREATE ROLE mini_test_repl WITH LOGIN REPLICATION PASSWORD %L', :'replication_password')
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'mini_test_repl')
\gexec
