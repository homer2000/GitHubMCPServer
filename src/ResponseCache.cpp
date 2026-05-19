#include "ResponseCache.h"

ResponseCache::ResponseCache(QObject *parent) : QObject(parent) {}

ResponseCache &ResponseCache::instance()
{
    static ResponseCache inst;
    return inst;
}

const CacheEntry *ResponseCache::find(const QString &url) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_store.find(url);
    if (it == m_store.end()) {
        ++const_cast<ResponseCache *>(this)->m_misses;
        return nullptr;
    }
    if (it->isExpired()) {
        const_cast<ResponseCache *>(this)->m_store.erase(it);
        const_cast<ResponseCache *>(this)->m_lruOrder.removeAll(url);
        ++const_cast<ResponseCache *>(this)->m_misses;
        return nullptr;
    }
    ++const_cast<ResponseCache *>(this)->m_hits;
    // touch LRU
    const_cast<ResponseCache *>(this)->m_lruOrder.removeAll(url);
    const_cast<ResponseCache *>(this)->m_lruOrder.append(url);
    return &it.value();
}

void ResponseCache::store(const QString &url, const QByteArray &body,
                           const QString &etag, const QString &lastModified,
                           const QString &contentType, int ttlSeconds)
{
    QMutexLocker lock(&m_mutex);
    if (!m_enabled) return;

    CacheEntry entry;
    entry.body          = body;
    entry.etag          = etag;
    entry.lastModified  = lastModified;
    entry.contentType   = contentType;
    entry.cachedAt      = QDateTime::currentDateTimeUtc();
    entry.ttlSeconds    = ttlSeconds > 0 ? ttlSeconds : m_defaultTtl;

    m_store[url]        = entry;
    m_lruOrder.removeAll(url);
    m_lruOrder.append(url);

    evictLru();
}

void ResponseCache::invalidate(const QString &urlPrefix)
{
    QMutexLocker lock(&m_mutex);
    QList<QString> toRemove;
    for (auto it = m_store.begin(); it != m_store.end(); ++it) {
        if (it.key().startsWith(urlPrefix))
            toRemove.append(it.key());
    }
    for (const QString &k : toRemove) {
        m_store.remove(k);
        m_lruOrder.removeAll(k);
    }
}

void ResponseCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_store.clear();
    m_lruOrder.clear();
    m_hits   = 0;
    m_misses = 0;
}

int ResponseCache::size() const
{
    QMutexLocker lock(&m_mutex);
    return m_store.size();
}

void ResponseCache::evictLru()
{
    while (m_store.size() > m_maxEntries && !m_lruOrder.isEmpty()) {
        QString oldest = m_lruOrder.takeFirst();
        m_store.remove(oldest);
    }
}
