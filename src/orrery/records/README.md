# Mission records: ports and adapters

The best mission times kept on this device. The records format itself (one
`id seconds` line per mission, frozen by `tests/fixtures/`) stays in
`orrery/missions.hpp`; this package decides where the records live and when
they are saved.

```
app/main.cpp ──▶ service (openDesktopRecords / unkeptRecords)
                   │ builds
                   ▼
                 RecordBook, recordsPath   (core: no I/O)
                   │ uses
                   ▼
                 ports/  RecordsStore, Environment
                   ▲ implement
                 adapters/  FileRecordsStore, NoRecordsStore, ProcessEnvironment
```

| Port | Question | Adapters (production) | Test fake |
|---|---|---|---|
| `RecordsStore` | Where do the best times live between runs? | `FileRecordsStore` (desktop `records.txt`), `NoRecordsStore` (headless, driven, no data directory, browser) | `FakeRecordsStore` in `tests/test_record_book.cpp` |
| `Environment` | What is this environment variable set to? | `ProcessEnvironment` (`std::getenv`) | `FakeEnvironment` in `tests/test_record_book.cpp` |

- **Core:** `RecordBook` loads the records when it is built, keeps a finished time only when it beats the record, then bumps `version()` (the browser page saves its localStorage copy when the version changes) and saves through the store. `recordsPath()` is the README's precedence: `$ORRERY_RECORDS`, else `%APPDATA%\orrery` on Windows, `$XDG_DATA_HOME/orrery` or `~/.local/share/orrery` elsewhere.
- **The browser** keeps its records in the page (`web/shell.html`, localStorage). The C++ side runs on `NoRecordsStore` and takes the page's records through `RecordBook::replace`.
- **Layers:** `record_book.*`, `records_path.*` and `ports/` never include `adapters/` or `service.*`; each port and adapter header declares one type. The adapters and the service build into `orrery_records_adapters`, which `orrery_core` does not link, and the `records_layers` ctest (`tools/check_layers.py`) checks the includes.
- **A new place to keep records** (a cloud save, say) is one new adapter file in `adapters/` implementing `RecordsStore`, plus the choice of it in `service.cpp`. It must pass the same contract test as the fake and `FileRecordsStore`.
