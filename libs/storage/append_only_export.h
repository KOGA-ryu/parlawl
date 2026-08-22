#pragma once

// The publication dance and the canonical-JSON helpers shared by every
// append-only ParlAWL journal.
//
// Extracted from `puzzle_attempt_repository.cpp` rather than copied. Two
// divergent copies of a symlink-refusing, no-overwrite, fsync-ordered
// publication is how one of them ends up wrong, and it would be the newer one.

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace parlawl::storage {

inline constexpr qsizetype kMaximumLineBytes = 1024 * 1024;
inline constexpr qsizetype kMaximumExportBytes = 64 * 1024 * 1024;
inline constexpr int kMaximumExportRecords = 100000;

bool fail(QString *errorMessage, const QString &message);

//! `<prefix><uuid>` with the exact canonical UUID spelling and nothing else.
bool exactOpaqueUuid(const QString &value, const QString &prefix);

QByteArray canonicalJson(const QJsonValue &value);
QString sha256Hex(const QByteArray &bytes);
QString semanticId(const QString &prefix, const QJsonObject &value);

//! Python's `datetime.isoformat`-compatible UTC spelling, microsecond field
//! present only when non-zero.
QString pythonUtc(const QDateTime &value);

bool parseCanonicalObject(
    const QByteArray &bytes,
    QJsonObject *object,
    QString *errorMessage,
    const QString &label);

//! Walk to `absolutePath` refusing a symlink at every component. Returns an
//! owned fd, or -1.
int openDirectoryWithoutSymlinks(const QString &absolutePath, QString *errorMessage);

//! Create, fill, fsync and atomically publish a new owner-only regular file.
//! Refuses an existing destination, including a symlink, rather than
//! overwriting it, and leaves nothing successful-looking behind on failure.
bool writeNewPrivateFile(
    const QString &path,
    const QByteArray &bytes,
    QString *errorMessage,
    const QByteArray &stagingPrefix = QByteArrayLiteral(".parlawl-attempt-export-"));

} // namespace parlawl::storage
