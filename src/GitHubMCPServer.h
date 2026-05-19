#pragma once

#include "GitHubAccount.h"
#include "GitHubAPI.h"

#include <QObject>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>

class GitHubMCPServer : public QObject
{
    Q_OBJECT
public:
    explicit GitHubMCPServer(QObject *parent = nullptr);

    void setConfigPath(const QString &path);
    void setDebug(bool debug) { m_debug = debug; }

    void run();

private:
    void processLine(const QString &line);
    void handleRequest(const QJsonObject &req);

    void sendResult(const QJsonValue &id, const QJsonValue &result);
    void sendError(const QJsonValue &id, int code, const QString &message);
    void sendResponse(const QJsonObject &obj);

    void handleInitialize(const QJsonValue &id, const QJsonObject &params);
    void handleToolsList(const QJsonValue &id, const QJsonObject &params);
    void handleToolsCall(const QJsonValue &id, const QJsonObject &params);

    QJsonObject dispatchTool(const QString &name, const QJsonObject &args);

    // ── Account ──────────────────────────────────────────────────────────────
    QJsonObject toolAccountList(const QJsonObject &a);
    QJsonObject toolAccountAdd(const QJsonObject &a);
    QJsonObject toolAccountRemove(const QJsonObject &a);

    // ── User ─────────────────────────────────────────────────────────────────
    QJsonObject toolGetAuthenticatedUser(const QJsonObject &a);
    QJsonObject toolGetUser(const QJsonObject &a);
    QJsonObject toolListUserRepos(const QJsonObject &a);
    QJsonObject toolListAuthenticatedUserRepos(const QJsonObject &a);
    QJsonObject toolGetUserOrgs(const QJsonObject &a);

    // ── Organizations ────────────────────────────────────────────────────────
    QJsonObject toolGetOrg(const QJsonObject &a);
    QJsonObject toolListOrgRepos(const QJsonObject &a);
    QJsonObject toolListOrgMembers(const QJsonObject &a);

    // ── Repos ────────────────────────────────────────────────────────────────
    QJsonObject toolReposList(const QJsonObject &a);
    QJsonObject toolRepoGet(const QJsonObject &a);
    QJsonObject toolRepoCreate(const QJsonObject &a);
    QJsonObject toolRepoCreateOrg(const QJsonObject &a);
    QJsonObject toolRepoDelete(const QJsonObject &a);
    QJsonObject toolRepoFork(const QJsonObject &a);
    QJsonObject toolListForks(const QJsonObject &a);
    QJsonObject toolListStargazers(const QJsonObject &a);
    QJsonObject toolListWatchers(const QJsonObject &a);
    QJsonObject toolStarRepo(const QJsonObject &a);
    QJsonObject toolUnstarRepo(const QJsonObject &a);
    QJsonObject toolListTopics(const QJsonObject &a);
    QJsonObject toolListContributors(const QJsonObject &a);
    QJsonObject toolListLanguages(const QJsonObject &a);

    // ── Branches ─────────────────────────────────────────────────────────────
    QJsonObject toolListBranches(const QJsonObject &a);
    QJsonObject toolGetBranch(const QJsonObject &a);
    QJsonObject toolCreateBranch(const QJsonObject &a);
    QJsonObject toolDeleteBranch(const QJsonObject &a);
    QJsonObject toolGetBranchProtection(const QJsonObject &a);

    // ── Tags & Commits ───────────────────────────────────────────────────────
    QJsonObject toolListTags(const QJsonObject &a);
    QJsonObject toolListCommits(const QJsonObject &a);
    QJsonObject toolGetCommit(const QJsonObject &a);
    QJsonObject toolCompareCommits(const QJsonObject &a);

    // ── Issues ───────────────────────────────────────────────────────────────
    QJsonObject toolIssuesList(const QJsonObject &a);
    QJsonObject toolIssueGet(const QJsonObject &a);
    QJsonObject toolIssueCreate(const QJsonObject &a);
    QJsonObject toolIssueUpdate(const QJsonObject &a);
    QJsonObject toolIssueClose(const QJsonObject &a);

    // ── Issue Comments ───────────────────────────────────────────────────────
    QJsonObject toolListIssueComments(const QJsonObject &a);
    QJsonObject toolGetIssueComment(const QJsonObject &a);
    QJsonObject toolCreateIssueComment(const QJsonObject &a);
    QJsonObject toolUpdateIssueComment(const QJsonObject &a);
    QJsonObject toolDeleteIssueComment(const QJsonObject &a);

    // ── Labels ───────────────────────────────────────────────────────────────
    QJsonObject toolListLabels(const QJsonObject &a);
    QJsonObject toolGetLabel(const QJsonObject &a);
    QJsonObject toolCreateLabel(const QJsonObject &a);
    QJsonObject toolUpdateLabel(const QJsonObject &a);
    QJsonObject toolDeleteLabel(const QJsonObject &a);

    // ── Milestones ───────────────────────────────────────────────────────────
    QJsonObject toolListMilestones(const QJsonObject &a);
    QJsonObject toolGetMilestone(const QJsonObject &a);
    QJsonObject toolCreateMilestone(const QJsonObject &a);
    QJsonObject toolUpdateMilestone(const QJsonObject &a);
    QJsonObject toolDeleteMilestone(const QJsonObject &a);

    // ── Pull Requests ────────────────────────────────────────────────────────
    QJsonObject toolPRsList(const QJsonObject &a);
    QJsonObject toolPRGet(const QJsonObject &a);
    QJsonObject toolPRCreate(const QJsonObject &a);
    QJsonObject toolPRUpdate(const QJsonObject &a);
    QJsonObject toolPRMerge(const QJsonObject &a);
    QJsonObject toolListPRComments(const QJsonObject &a);
    QJsonObject toolCreatePRComment(const QJsonObject &a);
    QJsonObject toolListPRReviews(const QJsonObject &a);
    QJsonObject toolListPRFiles(const QJsonObject &a);

    // ── Files ────────────────────────────────────────────────────────────────
    QJsonObject toolFileGet(const QJsonObject &a);
    QJsonObject toolFileCreate(const QJsonObject &a);
    QJsonObject toolFileUpdate(const QJsonObject &a);
    QJsonObject toolFileDelete(const QJsonObject &a);
    QJsonObject toolListDirectory(const QJsonObject &a);

    // ── Releases ─────────────────────────────────────────────────────────────
    QJsonObject toolListReleases(const QJsonObject &a);
    QJsonObject toolGetRelease(const QJsonObject &a);
    QJsonObject toolGetLatestRelease(const QJsonObject &a);
    QJsonObject toolGetReleaseByTag(const QJsonObject &a);
    QJsonObject toolCreateRelease(const QJsonObject &a);
    QJsonObject toolUpdateRelease(const QJsonObject &a);
    QJsonObject toolDeleteRelease(const QJsonObject &a);

    // ── Webhooks ─────────────────────────────────────────────────────────────
    QJsonObject toolListWebhooks(const QJsonObject &a);
    QJsonObject toolGetWebhook(const QJsonObject &a);
    QJsonObject toolCreateWebhook(const QJsonObject &a);
    QJsonObject toolUpdateWebhook(const QJsonObject &a);
    QJsonObject toolDeleteWebhook(const QJsonObject &a);
    QJsonObject toolPingWebhook(const QJsonObject &a);

    // ── Actions ──────────────────────────────────────────────────────────────
    QJsonObject toolListWorkflows(const QJsonObject &a);
    QJsonObject toolGetWorkflow(const QJsonObject &a);
    QJsonObject toolListWorkflowRuns(const QJsonObject &a);
    QJsonObject toolGetWorkflowRun(const QJsonObject &a);
    QJsonObject toolCancelWorkflowRun(const QJsonObject &a);
    QJsonObject toolRerunWorkflow(const QJsonObject &a);
    QJsonObject toolListRunArtifacts(const QJsonObject &a);

    // ── Gists ────────────────────────────────────────────────────────────────
    QJsonObject toolListGists(const QJsonObject &a);
    QJsonObject toolGetGist(const QJsonObject &a);
    QJsonObject toolCreateGist(const QJsonObject &a);
    QJsonObject toolUpdateGist(const QJsonObject &a);
    QJsonObject toolDeleteGist(const QJsonObject &a);
    QJsonObject toolListGistComments(const QJsonObject &a);
    QJsonObject toolCreateGistComment(const QJsonObject &a);
    QJsonObject toolDeleteGistComment(const QJsonObject &a);

    // ── Search ───────────────────────────────────────────────────────────────
    QJsonObject toolSearchCode(const QJsonObject &a);
    QJsonObject toolSearchRepos(const QJsonObject &a);
    QJsonObject toolSearchIssues(const QJsonObject &a);
    QJsonObject toolSearchUsers(const QJsonObject &a);
    QJsonObject toolSearchCommits(const QJsonObject &a);

    // ── Rate limit ───────────────────────────────────────────────────────────
    QJsonObject toolGetRateLimit(const QJsonObject &a);

    // ── Project upload ───────────────────────────────────────────────────────
    QJsonObject toolUploadProject(const QJsonObject &a);

    // ── Git sync ─────────────────────────────────────────────────────────────
    QJsonObject toolSyncRepository(const QJsonObject &a);

    // ── Logging ──────────────────────────────────────────────────────────────
    QJsonObject toolSetLogging(const QJsonObject &a);
    QJsonObject toolGetLoggingStatus(const QJsonObject &a);

    // ── Cache ────────────────────────────────────────────────────────────────
    QJsonObject toolSetCache(const QJsonObject &a);
    QJsonObject toolClearCache(const QJsonObject &a);
    QJsonObject toolGetCacheStats(const QJsonObject &a);
    QJsonObject toolInvalidateCache(const QJsonObject &a);

    // ── Pagination ───────────────────────────────────────────────────────────
    QJsonObject toolSetPagination(const QJsonObject &a);

    // ── Helpers ──────────────────────────────────────────────────────────────
    bool loadConfig();
    bool saveConfig();
    void createDefaultConfig();

    GitHubAPI  *apiForAccount(const QString &name);
    QJsonObject apiResponseToJson(const ApiResponse &resp);
    QJsonArray  buildToolsList() const;
    void        debugLog(const QString &msg);

    QString m_configPath;
    bool    m_debug = false;

    // Pagination defaults applied to new API instances
    bool m_defaultAutoFetch = false;
    int  m_defaultMaxPages  = 100;

    QMap<QString, GitHubAccount> m_accounts;
    QMap<QString, GitHubAPI *>   m_apis;

    QTextStream m_in;
    QTextStream m_out;
    QTextStream m_err;
};
