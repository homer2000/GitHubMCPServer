#include "GitHubAccount.h"

#include <QByteArray>
#include <QUrl>

GitHubAccount::GitHubAccount()
    : m_baseUrl("https://api.github.com")
    , m_type(Type::GitHub)
{}

GitHubAccount::GitHubAccount(const QString &name, const QString &token,
                              const QString &username, const QString &baseUrl, Type type)
    : m_name(name)
    , m_username(username)
    , m_baseUrl(baseUrl)
    , m_type(type)
{
    setToken(token);
}

QString GitHubAccount::token() const
{
    return decryptToken(m_encryptedToken);
}

void GitHubAccount::setToken(const QString &token)
{
    m_encryptedToken = encryptToken(token);
}

QString GitHubAccount::apiBaseUrl() const
{
    if (m_type == Type::GitHubEnterprise) {
        // Если пользователь указал "https://github.mycompany.com" — добавляем /api/v3
        // Если уже содержит /api/v3 — оставляем как есть
        if (m_baseUrl.contains("/api/v3") || m_baseUrl.contains("/api/v"))
            return m_baseUrl;
        QString base = m_baseUrl.trimmed();
        if (base.endsWith('/')) base.chop(1);
        return base + "/api/v3";
    }
    return m_baseUrl;
}

QJsonObject GitHubAccount::toJson() const
{
    QJsonObject obj;
    obj["name"]     = m_name;
    obj["type"]     = typeToString(m_type);
    obj["username"] = m_username;
    obj["token"]    = m_encryptedToken;   // encrypted
    obj["baseUrl"]  = m_baseUrl;
    return obj;
}

GitHubAccount GitHubAccount::fromJson(const QJsonObject &obj)
{
    GitHubAccount acc;
    acc.m_name     = obj["name"].toString();
    acc.m_username = obj.value("username").toString();
    acc.m_baseUrl  = obj.value("baseUrl").toString("https://api.github.com");
    acc.m_type     = typeFromString(obj.value("type").toString("github"));

    QString raw = obj["token"].toString();
    // Если токен в открытом виде — шифруем при загрузке
    if (raw.startsWith("ghp_")         ||
        raw.startsWith("github_pat_")  ||
        raw.startsWith("gho_")         ||
        raw.startsWith("ghx_")         ||
        raw.startsWith("ghs_"))
    {
        acc.m_encryptedToken = encryptToken(raw);
    } else {
        acc.m_encryptedToken = raw;
    }
    return acc;
}

// ── Encryption ────────────────────────────────────────────────────────────────

QString GitHubAccount::encryptToken(const QString &token)
{
    if (token.isEmpty()) return {};
    QByteArray bytes = token.toUtf8();
    for (int i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>(bytes[i] ^ XOR_KEY);
    return QString::fromLatin1(bytes.toBase64());
}

QString GitHubAccount::decryptToken(const QString &encrypted)
{
    if (encrypted.isEmpty()) return {};
    QByteArray bytes = QByteArray::fromBase64(encrypted.toLatin1());
    for (int i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>(bytes[i] ^ XOR_KEY);
    return QString::fromUtf8(bytes);
}

// ── Type helpers ──────────────────────────────────────────────────────────────

QString GitHubAccount::typeToString(Type t)
{
    switch (t) {
    case Type::GitHubEnterprise: return "github_enterprise";
    default:                     return "github";
    }
}

GitHubAccount::Type GitHubAccount::typeFromString(const QString &s)
{
    if (s == "github_enterprise" || s == "ghe") return Type::GitHubEnterprise;
    return Type::GitHub;
}
