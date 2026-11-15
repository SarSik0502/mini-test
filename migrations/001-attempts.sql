BEGIN;

CREATE TABLE IF NOT EXISTS attempts (
    id uuid PRIMARY KEY,
    job jsonb NOT NULL,
    score integer,
    total integer NOT NULL CHECK (total > 0),
    CHECK (score BETWEEN 0 AND total)
);

GRANT SELECT, INSERT, UPDATE ON attempts TO mini_test_app;

COMMIT;
