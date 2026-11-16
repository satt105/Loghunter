// SQLite schema of LogHunter projects.
#pragma once

#include <QString>
#include <QStringList>

namespace lh::schema {

inline constexpr int VERSION = 4;

inline const QString SQL_INSERT_EVENT = QStringLiteral(
    "INSERT OR IGNORE INTO events (group_id, ts_utc, day, hour, source_id, log_id, record_id, block, rank) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");

inline const QString SQL_INSERT_GROUP = QStringLiteral(
    "INSERT INTO groups (group_id, signature, event_id, level, audit, channel, provider, machine, user, is_system, "
    "process, action, action_cat, file_path, dir_path, extension, access, source_ip, destination_ip, detail) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20)");

inline const QString SQL_INSERT_BLOCK =
    QStringLiteral("INSERT INTO blocks (id, source_id, data) VALUES (?1, ?2, ?3)");

inline const QString SQL_SEARCH_TEXT = QStringLiteral(
    "concat_ws(' ', event_id, NULLIF(channel, ''), NULLIF(provider, ''), NULLIF(machine, ''), NULLIF(user, ''), "
    "NULLIF(process, ''), NULLIF(source_ip, ''), NULLIF(destination_ip, ''), NULLIF(file_path, ''), "
    "NULLIF(detail, ''))");

inline const QString SQL_INDEX_SEARCH =
    QStringLiteral("INSERT INTO search (rowid, text) SELECT group_id, ") + SQL_SEARCH_TEXT
    + QStringLiteral(" FROM groups WHERE group_id >= ?1");

inline const QString SQL_TABLES = QStringLiteral(R"(
CREATE TABLE IF NOT EXISTS events (
    id         INTEGER PRIMARY KEY,
    group_id   INTEGER NOT NULL,
    ts_utc     INTEGER,
    day        INTEGER,
    hour       INTEGER NOT NULL DEFAULT -1,
    source_id  INTEGER NOT NULL DEFAULT 0,
    log_id     INTEGER NOT NULL DEFAULT 0,
    record_id  INTEGER NOT NULL DEFAULT 0,
    block      INTEGER NOT NULL DEFAULT 0,
    rank       INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS groups (
    group_id        INTEGER PRIMARY KEY,
    signature       INTEGER NOT NULL,
    total           INTEGER NOT NULL DEFAULT 0,
    first_ts        INTEGER,
    last_ts         INTEGER,
    event_id        INTEGER NOT NULL DEFAULT 0,
    level           INTEGER NOT NULL DEFAULT 4,
    audit           INTEGER NOT NULL DEFAULT 0,
    channel         TEXT    NOT NULL DEFAULT '',
    provider        TEXT    NOT NULL DEFAULT '',
    machine         TEXT    NOT NULL DEFAULT '',
    user            TEXT    NOT NULL DEFAULT '',
    is_system       INTEGER NOT NULL DEFAULT 0,
    process         TEXT    NOT NULL DEFAULT '',
    action          TEXT    NOT NULL DEFAULT '',
    action_cat      TEXT    NOT NULL DEFAULT '',
    file_path       TEXT    NOT NULL DEFAULT '',
    dir_path        TEXT    NOT NULL DEFAULT '',
    extension       TEXT    NOT NULL DEFAULT '',
    access          TEXT    NOT NULL DEFAULT '',
    source_ip       TEXT    NOT NULL DEFAULT '',
    destination_ip  TEXT    NOT NULL DEFAULT '',
    detail          TEXT    NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS logs (
    id       INTEGER PRIMARY KEY,
    machine  TEXT NOT NULL,
    channel  TEXT NOT NULL,
    UNIQUE (machine, channel)
);
CREATE TABLE IF NOT EXISTS volumes (
    key     TEXT PRIMARY KEY,
    letter  TEXT NOT NULL
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS blocks (
    id         INTEGER PRIMARY KEY,
    source_id  INTEGER NOT NULL,
    data       BLOB    NOT NULL
);
CREATE TABLE IF NOT EXISTS sources (
    id               INTEGER PRIMARY KEY,
    path             TEXT    NOT NULL,
    name             TEXT    NOT NULL,
    fingerprint      TEXT    NOT NULL UNIQUE,
    size             INTEGER NOT NULL DEFAULT 0,
    imported_at      TEXT    NOT NULL DEFAULT '',
    record_count     INTEGER NOT NULL DEFAULT 0,
    event_count      INTEGER NOT NULL DEFAULT 0,
    duplicate_count  INTEGER NOT NULL DEFAULT 0,
    machine          TEXT    NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS meta (
    key    TEXT PRIMARY KEY,
    value  TEXT NOT NULL
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS rules (
    number       INTEGER PRIMARY KEY,
    id           TEXT    NOT NULL UNIQUE,
    title        TEXT    NOT NULL DEFAULT '',
    description  TEXT    NOT NULL DEFAULT '',
    level        INTEGER NOT NULL DEFAULT 2,
    tags         TEXT    NOT NULL DEFAULT '',
    author       TEXT    NOT NULL DEFAULT '',
    links        TEXT    NOT NULL DEFAULT '',
    origin       TEXT    NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS matches (
    rule   INTEGER NOT NULL,
    event  INTEGER NOT NULL,
    PRIMARY KEY (rule, event)
) WITHOUT ROWID;
CREATE INDEX IF NOT EXISTS idx_matches ON matches(event);
CREATE VIRTUAL TABLE IF NOT EXISTS search USING fts5(
    text, content='', contentless_delete=1, tokenize='unicode61 remove_diacritics 2');
CREATE UNIQUE INDEX IF NOT EXISTS idx_unique ON events(log_id, record_id, IFNULL(ts_utc / 1000000, 0))
    WHERE record_id > 0;
)");

inline const QString SQL_VIEWS = QStringLiteral(R"(
CREATE VIEW IF NOT EXISTS event_view AS
SELECT e.id AS id, e.group_id AS group_id, e.ts_utc AS ts_utc, e.day AS day, e.hour AS hour,
       lh_timestamp(e.ts_utc) AS timestamp, e.source_id AS source_id, e.record_id AS record_id, e.block AS block,
       e.rank AS rank,
       g.signature AS signature, g.event_id AS event_id, g.level AS level, g.audit AS audit, g.channel AS channel,
       g.provider AS provider, g.machine AS machine, g.user AS user, g.is_system AS is_system,
       g.process AS process, g.action AS action, g.action_cat AS action_cat, g.file_path AS file_path,
       g.dir_path AS dir_path, g.extension AS extension, g.access AS access, g.source_ip AS source_ip,
       g.destination_ip AS destination_ip, g.detail AS detail
FROM events e LEFT JOIN groups g ON g.group_id = e.group_id;
)");

struct Index
{
    const char* name;
    const char* target;
    bool unique = false;
};
inline constexpr Index SECONDARY_INDEXES[] = {
    {"idx_events_time", "events(ts_utc, group_id)"},
    {"idx_events_group", "events(group_id, ts_utc)"},
    {"idx_groups_signature", "groups(signature)", true},
    {"idx_groups_user", "groups(user)"},
    {"idx_groups_file", "groups(file_path)"},
    {"idx_groups_dir", "groups(dir_path)"},
};

} // namespace lh::schema
