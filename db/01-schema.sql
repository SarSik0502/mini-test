\getenv app_password APP_DB_PASSWORD

BEGIN;

CREATE ROLE mini_test_app LOGIN PASSWORD :'app_password';

CREATE TABLE questions (
    id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    prompt text NOT NULL CHECK (char_length(btrim(prompt)) BETWEEN 1 AND 500),
    options text[] NOT NULL CHECK (cardinality(options) = 3),
    correct_option smallint NOT NULL CHECK (correct_option BETWEEN 1 AND 3)
);

GRANT SELECT, INSERT, UPDATE, DELETE ON questions TO mini_test_app;

COMMIT;
