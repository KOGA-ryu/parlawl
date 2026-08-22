#include "append_only_export.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>

#include "strict_json.h"

namespace parlawl::storage {

namespace {

QString systemError(const QString &prefix)
{
    return QStringLiteral("%1: %2").arg(prefix, QString::fromLocal8Bit(std::strerror(errno)));
}

void appendJsonString(QByteArray *output, const QString &value)
{
    output->append('"');
    for (int index = 0; index < value.size(); ++index) {
        const ushort code = value.at(index).unicode();
        switch (code) {
        case '"': output->append("\\\""); continue;
        case '\\': output->append("\\\\"); continue;
        case '\b': output->append("\\b"); continue;
        case '\f': output->append("\\f"); continue;
        case '\n': output->append("\\n"); continue;
        case '\r': output->append("\\r"); continue;
        case '\t': output->append("\\t"); continue;
        default: break;
        }
        if (code < 0x20) {
            output->append(QStringLiteral("\\u%1").arg(code, 4, 16, QLatin1Char('0')).toLatin1());
            continue;
        }
        if (QChar::isHighSurrogate(code) && index + 1 < value.size()
            && QChar::isLowSurrogate(value.at(index + 1).unicode())) {
            output->append(value.mid(index, 2).toUtf8());
            ++index;
            continue;
        }
        output->append(QString(value.at(index)).toUtf8());
    }
    output->append('"');
}

bool pythonStringLess(const QString &left, const QString &right)
{
    const auto leftCodePoints = left.toUcs4();
    const auto rightCodePoints = right.toUcs4();
    return std::lexicographical_compare(
        leftCodePoints.cbegin(), leftCodePoints.cend(), rightCodePoints.cbegin(), rightCodePoints.cend());
}

void appendCanonical(QByteArray *output, const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        output->append("null");
        return;
    case QJsonValue::Bool:
        output->append(value.toBool() ? "true" : "false");
        return;
    case QJsonValue::Double: {
        const double number = value.toDouble();
        if (std::isfinite(number) && std::trunc(number) == number
            && number >= static_cast<double>(std::numeric_limits<qint64>::min())
            && number <= static_cast<double>(std::numeric_limits<qint64>::max())) {
            output->append(QByteArray::number(static_cast<qint64>(number)));
        } else {
            // Python's shortest round-trip float spelling, shared with the
            // strict import boundary. `'g', 17` is not that spelling: it turns
            // 0.8 into 0.80000000000000004, and the results contract requires a
            // line that Python's own json.dumps would have produced.
            output->append(parlawl::strictjson::pythonFloat(number));
        }
        return;
    }
    case QJsonValue::String:
        appendJsonString(output, value.toString());
        return;
    case QJsonValue::Array: {
        output->append('[');
        const QJsonArray array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (index > 0) {
                output->append(',');
            }
            appendCanonical(output, array.at(index));
        }
        output->append(']');
        return;
    }
    case QJsonValue::Object:
        break;
    }

    output->append('{');
    const QJsonObject object = value.toObject();
    QStringList keys = object.keys();
    std::sort(keys.begin(), keys.end(), pythonStringLess);
    for (qsizetype index = 0; index < keys.size(); ++index) {
        if (index > 0) {
            output->append(',');
        }
        appendJsonString(output, keys.at(index));
        output->append(':');
        appendCanonical(output, object.value(keys.at(index)));
    }
    output->append('}');
}

} // namespace

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

bool exactOpaqueUuid(const QString &value, const QString &prefix)
{
    static const QRegularExpression suffixPattern(QStringLiteral(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    return value.startsWith(prefix)
        && suffixPattern.match(value.mid(prefix.size())).hasMatch();
}

QByteArray canonicalJson(const QJsonValue &value)
{
    QByteArray result;
    appendCanonical(&result, value);
    return result;
}

QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString semanticId(const QString &prefix, const QJsonObject &value)
{
    return prefix + QLatin1Char(':') + sha256Hex(canonicalJson(value));
}

QString pythonUtc(const QDateTime &value)
{
    if (!value.isValid()) {
        return {};
    }
    const QDateTime utc = value.toUTC();
    QString result = utc.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"));
    if (utc.time().msec() != 0) {
        result += QLatin1Char('.') + QStringLiteral("%1").arg(utc.time().msec(), 3, 10, QLatin1Char('0'))
            + QStringLiteral("000");
    }
    return result + QLatin1Char('Z');
}

bool parseCanonicalObject(const QByteArray &bytes, QJsonObject *object, QString *errorMessage, const QString &label)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(errorMessage, QStringLiteral("%1 is not a JSON object: %2").arg(label, parseError.errorString()));
    }
    if (canonicalJson(document.object()) != bytes) {
        return fail(errorMessage, QStringLiteral("%1 is not canonical JSON").arg(label));
    }
    if (object != nullptr) {
        *object = document.object();
    }
    return true;
}

int openDirectoryWithoutSymlinks(const QString &absolutePath, QString *errorMessage)
{
    if (!QDir::isAbsolutePath(absolutePath)) {
        fail(errorMessage, QStringLiteral("export parent path must be absolute"));
        return -1;
    }
    int directoryFd = ::open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directoryFd < 0) {
        fail(errorMessage, systemError(QStringLiteral("failed to open filesystem root")));
        return -1;
    }
    const QStringList components = QDir::cleanPath(absolutePath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &component : components) {
        if (component == QStringLiteral(".") || component == QStringLiteral("..")) {
            ::close(directoryFd);
            fail(errorMessage, QStringLiteral("export parent contains an unsafe path component"));
            return -1;
        }
        const QByteArray encoded = QFile::encodeName(component);
        const int nextFd = ::openat(
            directoryFd, encoded.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (nextFd < 0) {
            const QString message = systemError(QStringLiteral("export parent contains a missing or symlink component"));
            ::close(directoryFd);
            fail(errorMessage, message);
            return -1;
        }
        ::close(directoryFd);
        directoryFd = nextFd;
    }
    struct stat directoryStat {};
    if (::fstat(directoryFd, &directoryStat) != 0 || !S_ISDIR(directoryStat.st_mode)) {
        const QString message = systemError(QStringLiteral("failed to verify export parent directory"));
        ::close(directoryFd);
        fail(errorMessage, message);
        return -1;
    }
    return directoryFd;
}

bool writeNewPrivateFile(
    const QString &path,
    const QByteArray &bytes,
    QString *errorMessage,
    const QByteArray &stagingPrefix)
{
    const QFileInfo targetInfo(path);
    const QString fileName = targetInfo.fileName();
    if (fileName.isEmpty() || fileName == QStringLiteral(".") || fileName == QStringLiteral("..")
        || fileName.contains(QLatin1Char('/'))) {
        return fail(errorMessage, QStringLiteral("export destination must name a direct file"));
    }
    const int parentFd = openDirectoryWithoutSymlinks(targetInfo.absoluteDir().absolutePath(), errorMessage);
    if (parentFd < 0) {
        return false;
    }
    const QByteArray encodedName = QFile::encodeName(fileName);
    struct stat targetStat {};
    if (::fstatat(parentFd, encodedName.constData(), &targetStat, AT_SYMLINK_NOFOLLOW) == 0) {
        ::close(parentFd);
        return fail(errorMessage, QStringLiteral("export destination already exists; choose a new file name"));
    }
    if (errno != ENOENT) {
        const QString message = systemError(QStringLiteral("failed to inspect export destination"));
        ::close(parentFd);
        return fail(errorMessage, message);
    }

    const QByteArray temporaryName = stagingPrefix
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1() + QByteArrayLiteral(".tmp");
    const int fileFd = ::openat(
        parentFd, temporaryName.constData(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fileFd < 0) {
        const QString message = systemError(QStringLiteral("failed to create private export staging file"));
        ::close(parentFd);
        return fail(errorMessage, message);
    }

    bool ok = ::fchmod(fileFd, S_IRUSR | S_IWUSR) == 0;
    qsizetype offset = 0;
    while (ok && offset < bytes.size()) {
        const ssize_t written = ::write(fileFd, bytes.constData() + offset, static_cast<size_t>(bytes.size() - offset));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok) {
        ok = ::fsync(fileFd) == 0;
    }
    const int savedErrno = errno;
    if (::close(fileFd) != 0 && ok) {
        ok = false;
    }
    if (!ok) {
        errno = savedErrno;
        ::unlinkat(parentFd, temporaryName.constData(), 0);
        const QString message = systemError(QStringLiteral("failed to publish complete export"));
        ::close(parentFd);
        return fail(errorMessage, message);
    }

    if (::linkat(parentFd, temporaryName.constData(), parentFd, encodedName.constData(), 0) != 0) {
        const QString message = errno == EEXIST
            ? QStringLiteral("export destination already exists; choose a new file name")
            : systemError(QStringLiteral("failed to atomically publish export"));
        ::unlinkat(parentFd, temporaryName.constData(), 0);
        ::close(parentFd);
        return fail(errorMessage, message);
    }
    if (::fsync(parentFd) != 0) {
        const int publicationErrno = errno;
        const bool removedFinal = ::unlinkat(parentFd, encodedName.constData(), 0) == 0;
        struct stat stagedFinal {};
        struct stat remainingFinal {};
        const bool completeFinalRemains = !removedFinal
            && ::fstatat(parentFd, temporaryName.constData(), &stagedFinal, AT_SYMLINK_NOFOLLOW) == 0
            && ::fstatat(parentFd, encodedName.constData(), &remainingFinal, AT_SYMLINK_NOFOLLOW) == 0
            && S_ISREG(remainingFinal.st_mode)
            && stagedFinal.st_dev == remainingFinal.st_dev
            && stagedFinal.st_ino == remainingFinal.st_ino;
        ::unlinkat(parentFd, temporaryName.constData(), 0);
        ::fsync(parentFd);
        ::close(parentFd);
        if (completeFinalRemains) {
            // The complete, fsynced inode is already published. Reporting a
            // failure would leave a successful-looking no-overwrite target
            // that the caller cannot retry, so publication is success here.
            return true;
        }
        errno = publicationErrno;
        const QString message = systemError(QStringLiteral("failed to sync published export directory"));
        return fail(errorMessage, message);
    }
    if (::unlinkat(parentFd, temporaryName.constData(), 0) == 0) {
        // The final link was already made durable above. A failure to persist
        // staging-name cleanup does not turn that complete publication into a
        // reported failure.
        ::fsync(parentFd);
    }
    ::close(parentFd);
    return true;
}

} // namespace parlawl::storage
