#pragma once

#include <QString>
#include <QJsonObject>

/**
 * GitHubAccount хранит учётные данные одного аккаунта GitHub / GitHub Enterprise.
 *
 * Поля:
 *   name     — произвольный псевдоним (ключ в конфиге)
 *   username — GitHub-логин пользователя (e.g. "octocat")
 *   token    — Personal Access Token (хранится XOR+base64, в JSON никогда plaintext)
 *   baseUrl  — "https://api.github.com" или "https://GHE_HOST/api/v3"
 *   type     — "github" | "github_enterprise"
 */
class GitHubAccount
{
public:
    enum class Type { GitHub, GitHubEnterprise };

    GitHubAccount();
    GitHubAccount(const QString &name, const QString &token,
                  const QString &username = QString(),
                  const QString &baseUrl  = "https://api.github.com",
                  Type type = Type::GitHub);

    // ── Accessors ────────────────────────────────────────────────────────────
    QString name()     const { return m_name; }
    QString username() const { return m_username; }
    QString token()    const;
    QString baseUrl()  const { return m_baseUrl; }
    Type    type()     const { return m_type; }
    bool    isEnterprise() const { return m_type == Type::GitHubEnterprise; }

    // Для GHE: возвращает "https://HOST/api/v3" на основе baseUrl
    // Для github.com: "https://api.github.com"
    QString apiBaseUrl() const;

    // ── Mutators ─────────────────────────────────────────────────────────────
    void setName(const QString &n)       { m_name = n; }
    void setUsername(const QString &u)   { m_username = u; }
    void setToken(const QString &t);
    void setBaseUrl(const QString &u)    { m_baseUrl = u; }
    void setType(Type t)                 { m_type = t; }

    bool isValid() const { return !m_name.isEmpty() && !m_encryptedToken.isEmpty(); }

    // ── Serialization ────────────────────────────────────────────────────────
    QJsonObject         toJson()              const;
    static GitHubAccount fromJson(const QJsonObject &obj);

    // ── Token encryption ─────────────────────────────────────────────────────
    static QString encryptToken(const QString &token);
    static QString decryptToken(const QString &encrypted);

    // Human-readable type string
    static QString typeToString(Type t);
    static Type    typeFromString(const QString &s);

private:
    QString m_name;
    QString m_username;
    QString m_encryptedToken;
    QString m_baseUrl;
    Type    m_type = Type::GitHub;

    static const quint8 XOR_KEY = 0x5A;
};
