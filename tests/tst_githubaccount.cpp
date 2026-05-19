#include <QtTest/QtTest>
#include "GitHubAccount.h"

class TestGitHubAccount : public QObject
{
    Q_OBJECT

private slots:

    void testDefaultConstructor()
    {
        GitHubAccount acc;
        QVERIFY(!acc.isValid());
        QCOMPARE(acc.baseUrl(), QString("https://api.github.com"));
        QCOMPARE(acc.type(), GitHubAccount::Type::GitHub);
        QVERIFY(!acc.isEnterprise());
    }

    void testConstructorWithArgs()
    {
        GitHubAccount acc("personal", "ghp_testtoken", "octocat");
        QVERIFY(acc.isValid());
        QCOMPARE(acc.name(),     QString("personal"));
        QCOMPARE(acc.username(), QString("octocat"));
        QCOMPARE(acc.token(),    QString("ghp_testtoken"));
        QCOMPARE(acc.baseUrl(),  QString("https://api.github.com"));
        QVERIFY(!acc.isEnterprise());
    }

    void testTokenEncryptionRoundtrip()
    {
        QString original = "ghp_ABCDEFghij1234567890XYZABC";
        QString encrypted = GitHubAccount::encryptToken(original);
        QVERIFY(encrypted != original);
        QVERIFY(!encrypted.isEmpty());
        QString decrypted = GitHubAccount::decryptToken(encrypted);
        QCOMPARE(decrypted, original);
    }

    void testTokenNotStoredPlaintext()
    {
        GitHubAccount acc("test", "ghp_supersecret", "user");
        QJsonObject j = acc.toJson();
        // Token in JSON must NOT be plaintext
        QVERIFY(j["token"].toString() != "ghp_supersecret");
        // But decrypted must match
        QCOMPARE(acc.token(), QString("ghp_supersecret"));
    }

    void testJsonRoundtrip()
    {
        GitHubAccount original("work", "ghp_worktoken", "workuser",
                               "https://api.github.com", GitHubAccount::Type::GitHub);
        QJsonObject json = original.toJson();
        GitHubAccount restored = GitHubAccount::fromJson(json);

        QCOMPARE(restored.name(),     original.name());
        QCOMPARE(restored.username(), original.username());
        QCOMPARE(restored.token(),    original.token());
        QCOMPARE(restored.baseUrl(),  original.baseUrl());
        QCOMPARE(restored.type(),     original.type());
    }

    void testGitHubEnterpriseType()
    {
        GitHubAccount acc("corp", "ghp_token", "john",
                          "https://github.corp.com",
                          GitHubAccount::Type::GitHubEnterprise);
        QVERIFY(acc.isEnterprise());
        QCOMPARE(acc.type(), GitHubAccount::Type::GitHubEnterprise);
        // apiBaseUrl should append /api/v3
        QCOMPARE(acc.apiBaseUrl(), QString("https://github.corp.com/api/v3"));
    }

    void testGitHubEnterpriseAlreadyHasApiV3()
    {
        GitHubAccount acc("corp2", "ghp_token", "john",
                          "https://github.corp.com/api/v3",
                          GitHubAccount::Type::GitHubEnterprise);
        // Should not double-append
        QCOMPARE(acc.apiBaseUrl(), QString("https://github.corp.com/api/v3"));
    }

    void testGitHubDotComApiBaseUrl()
    {
        GitHubAccount acc("personal", "ghp_token", "octocat");
        QCOMPARE(acc.apiBaseUrl(), QString("https://api.github.com"));
    }

    void testTypeStringRoundtrip()
    {
        QCOMPARE(GitHubAccount::typeFromString("github"),           GitHubAccount::Type::GitHub);
        QCOMPARE(GitHubAccount::typeFromString("github_enterprise"),GitHubAccount::Type::GitHubEnterprise);
        QCOMPARE(GitHubAccount::typeFromString("ghe"),              GitHubAccount::Type::GitHubEnterprise);
        QCOMPARE(GitHubAccount::typeFromString("unknown"),          GitHubAccount::Type::GitHub);

        QCOMPARE(GitHubAccount::typeToString(GitHubAccount::Type::GitHub),           QString("github"));
        QCOMPARE(GitHubAccount::typeToString(GitHubAccount::Type::GitHubEnterprise), QString("github_enterprise"));
    }

    void testFromJsonWithPlaintextToken()
    {
        // When config contains a plaintext token starting with ghp_, it should be transparently encrypted
        QJsonObject obj;
        obj["name"]     = "test";
        obj["username"] = "user";
        obj["token"]    = "ghp_plaintext_token";
        obj["baseUrl"]  = "https://api.github.com";
        obj["type"]     = "github";

        GitHubAccount acc = GitHubAccount::fromJson(obj);
        QCOMPARE(acc.token(), QString("ghp_plaintext_token"));
        QVERIFY(acc.isValid());
    }

    void testSetters()
    {
        GitHubAccount acc;
        acc.setName("myacc");
        acc.setUsername("me");
        acc.setToken("ghp_mytoken");
        acc.setBaseUrl("https://api.github.com");
        acc.setType(GitHubAccount::Type::GitHub);

        QCOMPARE(acc.name(),     QString("myacc"));
        QCOMPARE(acc.username(), QString("me"));
        QCOMPARE(acc.token(),    QString("ghp_mytoken"));
        QVERIFY(acc.isValid());
    }
};

QTEST_MAIN(TestGitHubAccount)
#include "tst_githubaccount.moc"
