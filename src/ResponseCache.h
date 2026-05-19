#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QMutex>
#include <QDateTime>
#include <QJsonDocument>
#include <QByteArray>

/**
 * ResponseCache — in-memory LRU-кэш для GET-ответов GitHub API.
 *
 * Поддерживает:
 *   - TTL (time-to-live) на каждую запись
 *   - ETag / Last-Modified для conditional requests (HTTP 304)
 *   - Ограничение по количеству записей (LRU eviction)
 *   - Ручную инвалидацию по префиксу URL
 */
struct CacheEntry {
    QByteArray  body;
    QString     etag;
    QString     lastModified;
    QString     contentType;
    QDateTime   cachedAt;
    int         ttlSeconds = 60;

    bool isExpired() const {
        return cachedAt.secsTo(QDateTime::currentDateTimeUtc()) >= ttlSeconds;
    }
};

class ResponseCache : public QObject
{
    Q_OBJECT

public:
    static ResponseCache &instance();

    void setEnabled(bool en)       { m_enabled = en; }
    void setMaxEntries(int n)      { m_maxEntries = n; }
    void setDefaultTtl(int secs)   { m_defaultTtl = secs; }
    bool isEnabled() const         { return m_enabled; }

    // Поиск в кэше. Возвращает nullptr если нет / просрочено.
    const CacheEntry *find(const QString &url) const;

    // Сохранить ответ в кэш.
    void store(const QString &url, const QByteArray &body,
               const QString &etag, const QString &lastModified,
               const QString &contentType, int ttlSeconds = -1);

    // Инвалидировать все записи у которых URL начинается с prefix.
    void invalidate(const QString &urlPrefix);

    // Полная очистка.
    void clear();

    // Статистика.
    int  size()   const;
    int  hits()   const { return m_hits; }
    int  misses() const { return m_misses; }

private:
    explicit ResponseCache(QObject *parent = nullptr);

    void evictLru();

    mutable QMutex              m_mutex;
    QHash<QString, CacheEntry>  m_store;
    QList<QString>              m_lruOrder;  // front = oldest
    bool  m_enabled    = false;
    int   m_maxEntries = 512;
    int   m_defaultTtl = 60;
    int   m_hits       = 0;
    int   m_misses     = 0;
};

#define gResponseCache ResponseCache::instance()
