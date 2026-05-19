#pragma once

#include "GitHubAccount.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>

struct ApiResponse {
    bool        success  = false;
    int         httpCode = 0;
    QJsonObject data;
    QJsonArray  array;
    QString     errorMessage;
    bool        isArray  = false;

    // Pagination meta (заполняется при использовании fetchAllPages)
    int  totalPages    = 0;
    int  totalItems    = 0;
    bool truncated     = false;  // true если перестали забирать страницы из-за лимита
};

class GitHubAPI : public QObject
{
    Q_OBJECT

public:
    explicit GitHubAPI(QObject *parent = nullptr);

    void setAccount(const GitHubAccount &account);
    const GitHubAccount &account() const { return m_account; }

    // ── Pagination settings ──────────────────────────────────────────────────
    // fetchAllPages: если true — автоматически собирает все страницы в array
    void setAutoFetchAllPages(bool v)   { m_autoFetchAll = v; }
    bool autoFetchAllPages() const      { return m_autoFetchAll; }
    void setMaxPages(int n)             { m_maxPages = n; }    // защита от бесконечного цикла

    // ── User ─────────────────────────────────────────────────────────────────
    ApiResponse getAuthenticatedUser();
    ApiResponse getUser(const QString &username);
    ApiResponse listUserRepos(const QString &username, const QString &type = "all",
                              int perPage = 30, int page = 1);
    ApiResponse listAuthenticatedUserRepos(const QString &type = "all",
                                           int perPage = 30, int page = 1);
    ApiResponse getUserOrgs(const QString &username);

    // ── Organizations ────────────────────────────────────────────────────────
    ApiResponse getOrg(const QString &org);
    ApiResponse listOrgRepos(const QString &org, const QString &type = "all",
                             int perPage = 30, int page = 1);
    ApiResponse listOrgMembers(const QString &org);

    // ── Repositories ─────────────────────────────────────────────────────────
    ApiResponse getRepo(const QString &owner, const QString &repo);
    ApiResponse createRepo(const QString &name, const QString &description, bool isPrivate);
    ApiResponse createOrgRepo(const QString &org, const QString &name,
                              const QString &description, bool isPrivate);
    ApiResponse deleteRepo(const QString &owner, const QString &repo);
    ApiResponse forkRepo(const QString &owner, const QString &repo, const QString &org = "");
    ApiResponse listForks(const QString &owner, const QString &repo);
    ApiResponse listStargazers(const QString &owner, const QString &repo);
    ApiResponse listWatchers(const QString &owner, const QString &repo);
    ApiResponse starRepo(const QString &owner, const QString &repo);
    ApiResponse unstarRepo(const QString &owner, const QString &repo);
    ApiResponse listTopics(const QString &owner, const QString &repo);
    ApiResponse listContributors(const QString &owner, const QString &repo);
    ApiResponse listLanguages(const QString &owner, const QString &repo);

    // ── Branches ─────────────────────────────────────────────────────────────
    ApiResponse listBranches(const QString &owner, const QString &repo);
    ApiResponse getBranch(const QString &owner, const QString &repo, const QString &branch);
    ApiResponse createBranch(const QString &owner, const QString &repo,
                             const QString &branch, const QString &fromSha);
    ApiResponse deleteBranch(const QString &owner, const QString &repo, const QString &branch);
    ApiResponse getBranchProtection(const QString &owner, const QString &repo, const QString &branch);

    // ── Tags & Commits ───────────────────────────────────────────────────────
    ApiResponse listTags(const QString &owner, const QString &repo);
    ApiResponse listCommits(const QString &owner, const QString &repo,
                            const QString &sha = "", const QString &path = "",
                            const QString &author = "", int perPage = 30, int page = 1);
    ApiResponse getCommit(const QString &owner, const QString &repo, const QString &sha);
    ApiResponse compareCommits(const QString &owner, const QString &repo,
                               const QString &base, const QString &head);

    // ── Issues ───────────────────────────────────────────────────────────────
    ApiResponse listIssues(const QString &owner, const QString &repo,
                           const QString &state = "open", const QString &labels = "",
                           int perPage = 30, int page = 1);
    ApiResponse getIssue(const QString &owner, const QString &repo, int number);
    ApiResponse createIssue(const QString &owner, const QString &repo,
                            const QString &title, const QString &body, const QString &labels,
                            int milestone = 0, const QString &assignees = "");
    ApiResponse updateIssue(const QString &owner, const QString &repo, int number,
                            const QString &state, const QString &title, const QString &body);
    ApiResponse closeIssue(const QString &owner, const QString &repo, int number);

    // ── Issue Comments ───────────────────────────────────────────────────────
    ApiResponse listIssueComments(const QString &owner, const QString &repo, int issueNumber);
    ApiResponse getIssueComment(const QString &owner, const QString &repo, int commentId);
    ApiResponse createIssueComment(const QString &owner, const QString &repo,
                                   int issueNumber, const QString &body);
    ApiResponse updateIssueComment(const QString &owner, const QString &repo,
                                   int commentId, const QString &body);
    ApiResponse deleteIssueComment(const QString &owner, const QString &repo, int commentId);

    // ── Labels ───────────────────────────────────────────────────────────────
    ApiResponse listLabels(const QString &owner, const QString &repo);
    ApiResponse getLabel(const QString &owner, const QString &repo, const QString &name);
    ApiResponse createLabel(const QString &owner, const QString &repo,
                            const QString &name, const QString &color, const QString &description);
    ApiResponse updateLabel(const QString &owner, const QString &repo, const QString &name,
                            const QString &newName, const QString &color, const QString &description);
    ApiResponse deleteLabel(const QString &owner, const QString &repo, const QString &name);

    // ── Milestones ───────────────────────────────────────────────────────────
    ApiResponse listMilestones(const QString &owner, const QString &repo,
                               const QString &state = "open");
    ApiResponse getMilestone(const QString &owner, const QString &repo, int number);
    ApiResponse createMilestone(const QString &owner, const QString &repo,
                                const QString &title, const QString &description,
                                const QString &dueOn = "");
    ApiResponse updateMilestone(const QString &owner, const QString &repo, int number,
                                const QString &title, const QString &description,
                                const QString &state, const QString &dueOn);
    ApiResponse deleteMilestone(const QString &owner, const QString &repo, int number);

    // ── Pull Requests ─────────────────────────────────────────────────────────
    ApiResponse listPRs(const QString &owner, const QString &repo,
                        const QString &state = "open", int perPage = 30, int page = 1);
    ApiResponse getPR(const QString &owner, const QString &repo, int prNumber);
    ApiResponse createPR(const QString &owner, const QString &repo,
                         const QString &title, const QString &head,
                         const QString &base, const QString &body);
    ApiResponse updatePR(const QString &owner, const QString &repo, int prNumber,
                         const QString &title, const QString &body, const QString &state);
    ApiResponse mergePR(const QString &owner, const QString &repo,
                        int prNumber, const QString &mergeMethod);
    ApiResponse listPRComments(const QString &owner, const QString &repo, int prNumber);
    ApiResponse createPRComment(const QString &owner, const QString &repo,
                                int prNumber, const QString &body);
    ApiResponse listPRReviews(const QString &owner, const QString &repo, int prNumber);
    ApiResponse listPRFiles(const QString &owner, const QString &repo, int prNumber);

    // ── Files / Contents ──────────────────────────────────────────────────────
    ApiResponse getFile(const QString &owner, const QString &repo,
                        const QString &path, const QString &ref = "");
    ApiResponse createFile(const QString &owner, const QString &repo,
                           const QString &path, const QString &message, const QString &content,
                           const QString &branch = "");
    ApiResponse updateFile(const QString &owner, const QString &repo,
                           const QString &path, const QString &message,
                           const QString &content, const QString &sha,
                           const QString &branch = "");
    ApiResponse deleteFile(const QString &owner, const QString &repo,
                           const QString &path, const QString &message,
                           const QString &sha, const QString &branch = "");
    ApiResponse listDirectory(const QString &owner, const QString &repo,
                              const QString &path, const QString &ref = "");

    // ── Releases ──────────────────────────────────────────────────────────────
    ApiResponse listReleases(const QString &owner, const QString &repo, int perPage = 30);
    ApiResponse getRelease(const QString &owner, const QString &repo, int releaseId);
    ApiResponse getLatestRelease(const QString &owner, const QString &repo);
    ApiResponse getReleaseByTag(const QString &owner, const QString &repo, const QString &tag);
    ApiResponse createRelease(const QString &owner, const QString &repo,
                              const QString &tagName, const QString &name,
                              const QString &body, bool draft, bool prerelease,
                              const QString &targetCommitish = "");
    ApiResponse updateRelease(const QString &owner, const QString &repo, int releaseId,
                              const QString &tagName, const QString &name,
                              const QString &body, bool draft, bool prerelease);
    ApiResponse deleteRelease(const QString &owner, const QString &repo, int releaseId);

    // ── Webhooks ──────────────────────────────────────────────────────────────
    ApiResponse listWebhooks(const QString &owner, const QString &repo);
    ApiResponse getWebhook(const QString &owner, const QString &repo, int hookId);
    ApiResponse createWebhook(const QString &owner, const QString &repo,
                              const QString &url, const QStringList &events,
                              const QString &contentType, bool active);
    ApiResponse updateWebhook(const QString &owner, const QString &repo, int hookId,
                              const QString &url, const QStringList &events, bool active);
    ApiResponse deleteWebhook(const QString &owner, const QString &repo, int hookId);
    ApiResponse pingWebhook(const QString &owner, const QString &repo, int hookId);

    // ── Actions / Workflows ───────────────────────────────────────────────────
    ApiResponse listWorkflows(const QString &owner, const QString &repo);
    ApiResponse getWorkflow(const QString &owner, const QString &repo, const QString &workflowId);
    ApiResponse listWorkflowRuns(const QString &owner, const QString &repo,
                                 const QString &workflowId = "", const QString &status = "",
                                 int perPage = 30);
    ApiResponse getWorkflowRun(const QString &owner, const QString &repo, int runId);
    ApiResponse cancelWorkflowRun(const QString &owner, const QString &repo, int runId);
    ApiResponse rerunWorkflow(const QString &owner, const QString &repo, int runId);
    ApiResponse listRunArtifacts(const QString &owner, const QString &repo, int runId);

    // ── Gists ────────────────────────────────────────────────────────────────
    ApiResponse listGists(int perPage = 30);
    ApiResponse getGist(const QString &gistId);
    ApiResponse createGist(const QString &description, bool isPublic,
                           const QString &filename, const QString &content);
    ApiResponse updateGist(const QString &gistId, const QString &description,
                           const QString &filename, const QString &content);
    ApiResponse deleteGist(const QString &gistId);
    ApiResponse listGistComments(const QString &gistId);
    ApiResponse createGistComment(const QString &gistId, const QString &body);
    ApiResponse deleteGistComment(const QString &gistId, int commentId);

    // ── Search ───────────────────────────────────────────────────────────────
    ApiResponse searchCode(const QString &query, int perPage = 30);
    ApiResponse searchRepos(const QString &query, int perPage = 30);
    ApiResponse searchIssues(const QString &query, int perPage = 30);
    ApiResponse searchUsers(const QString &query, int perPage = 30);
    ApiResponse searchCommits(const QString &query, int perPage = 30);

    // ── Rate limit ───────────────────────────────────────────────────────────
    ApiResponse getRateLimit();

    // ── Git repository sync ──────────────────────────────────────────────────
    struct SyncResult {
        bool    success    = false;
        QString output;        // combined stdout+stderr from git
        QString errorMessage;
        // per-step results
        struct Step { QString name; bool ok; QString output; };
        QList<Step> steps;
    };
    // Синхронизирует локальный git-репозиторий с удалённым.
    // Токен и username берутся из текущего аккаунта автоматически.
    // remote: имя remote (default "origin"); если пустой remote.url — выставляется из аккаунта+owner+repo.
    SyncResult syncRepository(const QString &localRepoPath,
                               const QString &owner,
                               const QString &repo,
                               const QString &branch      = "main",
                               const QString &remote      = "origin",
                               const QString &commitMsg   = "Sync",
                               bool           forcePush   = false);

    // ── Project upload ───────────────────────────────────────────────────────
    struct UploadResult {
        bool    success      = false;
        int     uploaded     = 0;
        int     skipped      = 0;
        int     failed       = 0;
        QString errorMessage;
        QJsonArray log;
    };
    UploadResult uploadProject(const QString &localPath,
                               const QString &owner, const QString &repo,
                               const QString &branch      = "main",
                               const QString &commitPrefix = "Upload",
                               const QStringList &ignore   = {});

    // ── Cache management ─────────────────────────────────────────────────────
    void invalidateCacheFor(const QString &owner, const QString &repo = QString());

private:
    // HTTP primitives
    ApiResponse get(const QString &endpoint, const QString &queryString = "");
    ApiResponse post(const QString &endpoint, const QJsonObject &body);
    ApiResponse put(const QString &endpoint, const QJsonObject &body = QJsonObject());
    ApiResponse patch(const QString &endpoint, const QJsonObject &body);
    ApiResponse del(const QString &endpoint, const QJsonObject &body = QJsonObject());

    // Core send (single request)
    ApiResponse sendRequest(const QNetworkRequest &request, const QByteArray &verb,
                            const QByteArray &data = QByteArray());

    // Auto-pagination: collects all pages for a list endpoint
    ApiResponse fetchAllPages(const QString &endpoint, const QString &baseQuery);

    // Helpers
    QNetworkRequest buildRequest(const QString &url,
                                 const QString &etag = QString(),
                                 const QString &lastModified = QString()) const;
    ApiResponse     parseReply(QNetworkReply *reply, const QByteArray &rawBody,
                               qint64 elapsedMs) const;

    // Extract next-page URL from Link header
    static QString parseLinkNext(const QByteArray &linkHeader);

    QString baseUrl() const { return m_account.apiBaseUrl(); }

    QNetworkAccessManager m_nam;
    GitHubAccount         m_account;
    bool  m_autoFetchAll = false;
    int   m_maxPages     = 100;
};
