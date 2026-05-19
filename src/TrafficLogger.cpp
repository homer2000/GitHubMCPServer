#include "TrafficLogger.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QNetworkReply>   // нужен только для rawHeaderPairs() в GitHubAPI.cpp; здесь — для полноты типа

TrafficLogger::TrafficLogger(QObject *parent) : QObject(parent) {}

TrafficLogger::~TrafficLogger()
{
    if (m_file.isOpen()) m_file.close();
}

TrafficLogger &TrafficLogger::instance()
{
    static TrafficLogger inst;
    return inst;
}

void TrafficLogger::setLogFile(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    if (m_file.isOpen()) m_file.close();
    if (path.isEmpty()) return;

    m_file.setFileName(path);
    if (m_file.open(QIODevice::Append | QIODevice::Text)) {
        m_stream.setDevice(&m_file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        m_stream.setCodec("UTF-8");
#endif
    }
}

void TrafficLogger::setLogLevel(LogLevel level) { m_level = level; }
void TrafficLogger::setMaxBodyBytes(int bytes)   { m_maxBody = bytes; }

void TrafficLogger::write(const QString &text)
{
    QMutexLocker lock(&m_mutex);
    if (!m_file.isOpen()) return;
    m_stream << text;
    m_stream.flush();
}

QString TrafficLogger::maskToken(const QString &header)
{
    // "Bearer ghp_XXXXXXXXXXXX" → "Bearer gh***hidden***"
    static QRegularExpression re(R"((Bearer\s+)\S{4}(\S+))");
    return QString(header).replace(re, R"(\1****hidden****)");
}

void TrafficLogger::logRequest(const QString &verb, const QNetworkRequest &req,
                               const QByteArray &body)
{
    if (m_level == LogLevel::None) return;

    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString out;
    out += QString("\n%1\n").arg(QString(60, '-'));
    out += QString("[%1] >>> %2  %3\n").arg(ts, verb, req.url().toString());

    if (m_level >= LogLevel::Headers) {
        const auto &rawHeaders = req.rawHeaderList();
        for (const QByteArray &name : rawHeaders) {
            QString val = QString::fromUtf8(req.rawHeader(name));
            if (name.toLower() == "authorization")
                val = maskToken(val);
            out += QString("  %1: %2\n").arg(QString::fromUtf8(name), val);
        }
    }

    if (m_level >= LogLevel::Full && !body.isEmpty()) {
        out += "\n";
        QByteArray truncated = body.left(m_maxBody);
        out += QString::fromUtf8(truncated);
        if (body.size() > m_maxBody)
            out += QString("\n  ... [%1 bytes truncated]").arg(body.size() - m_maxBody);
        out += "\n";
    }

    write(out);
}

void TrafficLogger::logResponse(const QString &verb, const QUrl &url,
                                int httpCode, const QByteArray &body,
                                qint64 elapsedMs,
                                const QList<RawHeaderPair> &headers)
{
    if (m_level == LogLevel::None) return;

    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString out;
    out += QString("[%1] <<< %2  %3  (%4 ms)  [%5]\n")
               .arg(ts)
               .arg(httpCode)
               .arg(url.path())
               .arg(elapsedMs)
               .arg(verb);

    if (m_level >= LogLevel::Headers) {
        for (const auto &pair : headers) {
            out += QString("  %1: %2\n")
                       .arg(QString::fromUtf8(pair.first),
                            QString::fromUtf8(pair.second));
        }
    }

    if (m_level >= LogLevel::Full && !body.isEmpty()) {
        out += "\n";
        QByteArray truncated = body.left(m_maxBody);
        out += QString::fromUtf8(truncated);
        if (body.size() > m_maxBody)
            out += QString("\n  ... [%1 bytes truncated]").arg(body.size() - m_maxBody);
        out += "\n";
    }

    write(out);
}
