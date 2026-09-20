# SQLite compatibility fixtures

These databases were created with the QtSql implementation from the commit immediately before the native SQLite cleanup. Keep them as historical inputs; do not regenerate them with the native repository.

- `qt-v10.db`: migrations 1–10 applied by the old QtSql migration runner, with a UTF-8 group, term, content, and alias. Opening it with the native repository must apply migrations 11–20 and preserve the data.
- `qt-v20.db`: migrations 1–20 applied by the old QtSql runner, populated through the old application integration test. It contains groups, items, a link, a typed value, UTF-8 configuration, and enum options serialized by `QJsonDocument`.

`SqliteCompatibility.cpp` copies each fixture to a temporary directory before opening or modifying it. Both fixture files pass SQLite `PRAGMA integrity_check`.
