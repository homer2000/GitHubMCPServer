#pragma once

#include <QObject>
#include <QString>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QNetworkRequest>
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QUrl>

/**
 * TrafficLogger — потокобезопасный логгер HTTP-трафика.
 *
 * Записывает каждый запрос и ответ в текстовый файл в формате:
 *
 *   ──────────────────────────────────────────
 *   [2024-01-15 12:34:56.789] >>> GET https://api.github.com/repos/owner/repo
 *   Authorization: Bearer gh***hidden***
 *   Accept: application/vnd.github+json
 *
 *   ──────────────────────────────────────────
 *   [2024-01-15 12:34:56.912] <<< 200 OK  (123 ms)
 *   Content-Type: application/json
 *
 *   {"id":123,...}
 *
 * Токен в заголовке Authorization автоматически маскируется.
 */

// QNetworkReply::RawHeaderPair = QPair<QByteArray, QByteArray>
// Определяем здесь, чтобы не подключать тяжёлый QNetworkReply в заголовок
using RawHeaderPair = QPair<QByteArray, QByteArray>;

class TrafficLogger : public QObject
{
    Q_OBJECT

public:
    enum class LogLevel {
        None    = 0,  // логирование выключено
        Summary = 1,  // только метод + URL + HTTP-статус
        Headers = 2,  // Summary + заголовки
        Full    = 3   // Headers + тела запросов и ответов
    };

    static TrafficLogger &instance();

    // Настройка
    void setLogFile(const QString &path);
    void setLogLevel(LogLevel level);
    void setMaxBodyBytes(int bytes);  // ограничение длины тела в логе (default 4096)
    LogLevel logLevel() const { return m_level; }

    // Вызываются из GitHubAPI
    void logRequest(const QString &verb, const QNetworkRequest &req,
                    const QByteArray &body = QByteArray());
    void logResponse(const QString &verb, const QUrl &url,
                     int httpCode, const QByteArray &body,
                     qint64 elapsedMs,
                     const QList<RawHeaderPair> &headers = {});

private:
    explicit TrafficLogger(QObject *parent = nullptr);
    ~TrafficLogger();

    void write(const QString &text);
    static QString maskToken(const QString &header);

    QMutex      m_mutex;
    QFile       m_file;
    QTextStream m_stream;
    LogLevel    m_level   = LogLevel::None;
    int         m_maxBody = 4096;
};

// Convenience: include this header to access the singleton
#define gTrafficLogger TrafficLogger::instance()
