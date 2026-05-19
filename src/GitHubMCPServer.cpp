#include "GitHubMCPServer.h"
#include "TrafficLogger.h"
#include "ResponseCache.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QSocketNotifier>
#include <stdio.h>

// ── Macros ───────────────────────────────────────────────────────────────────

#define REQUIRE_API \
    GitHubAPI *api = apiForAccount(args.value("account").toString()); \
    if (!api) { QJsonObject r; r["success"]=false; r["error"]="No account configured"; return r; }

// ── Constructor ───────────────────────────────────────────────────────────────

GitHubMCPServer::GitHubMCPServer(QObject *parent)
    : QObject(parent)
    , m_in(stdin,  QIODevice::ReadOnly)
    , m_out(stdout, QIODevice::WriteOnly)
    , m_err(stderr, QIODevice::WriteOnly)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    m_in.setCodec("UTF-8");
    m_out.setCodec("UTF-8");
    m_err.setCodec("UTF-8");
#endif
    // Qt 6: UTF-8 is the default encoding for QTextStream
}

void GitHubMCPServer::setConfigPath(const QString &path) { m_configPath = path; }

// ── Main loop ─────────────────────────────────────────────────────────────────

void GitHubMCPServer::run()
{
    if (m_configPath.isEmpty())
        m_configPath = QDir::currentPath() + "/accounts.json";
    if (!loadConfig()) { createDefaultConfig(); loadConfig(); }
    debugLog("GitHub MCP Server started. Config: " + m_configPath);

    QSocketNotifier *sn = new QSocketNotifier(fileno(stdin), QSocketNotifier::Read, this);
    connect(sn, &QSocketNotifier::activated, this, [this]() {
        while (!m_in.atEnd()) {
            QString line = m_in.readLine().trimmed();
            if (!line.isEmpty()) processLine(line);
        }
    });
}

// ── Protocol ─────────────────────────────────────────────────────────────────

void GitHubMCPServer::processLine(const QString &line)
{
    debugLog("<<<< " + line);
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        sendError(QJsonValue::Undefined, -32700, "Parse error"); return;
    }
    handleRequest(doc.object());
}

void GitHubMCPServer::handleRequest(const QJsonObject &req)
{
    QJsonValue id     = req.value("id");
    QString    method = req.value("method").toString();
    if      (method == "initialize")    handleInitialize(id, req.value("params").toObject());
    else if (method == "tools/list")    handleToolsList(id, req.value("params").toObject());
    else if (method == "tools/call")    handleToolsCall(id, req.value("params").toObject());
    else if (method.startsWith("notifications/")) { /* no-op */ }
    else    sendError(id, -32601, "Method not found: " + method);
}

void GitHubMCPServer::handleInitialize(const QJsonValue &id, const QJsonObject &)
{
    QJsonObject caps, tools;
    tools["listChanged"] = false;
    caps["tools"] = tools;
    QJsonObject si;
    si["name"] = "github-mcp-server"; si["version"] = "2.0.0";
    QJsonObject res;
    res["protocolVersion"] = "2024-11-05";
    res["capabilities"]    = caps;
    res["serverInfo"]      = si;
    sendResult(id, res);
}

void GitHubMCPServer::handleToolsList(const QJsonValue &id, const QJsonObject &)
{
    QJsonObject res; res["tools"] = buildToolsList();
    sendResult(id, res);
}

void GitHubMCPServer::handleToolsCall(const QJsonValue &id, const QJsonObject &params)
{
    QString     name = params.value("name").toString();
    QJsonObject args = params.value("arguments").toObject();
    debugLog("Tool: " + name);
    QJsonObject toolResult = dispatchTool(name, args);

    QJsonObject tb; tb["type"] = "text";
    tb["text"] = QString::fromUtf8(QJsonDocument(toolResult).toJson(QJsonDocument::Indented));
    QJsonArray content; content.append(tb);
    QJsonObject res;
    res["content"] = content;
    res["isError"] = !toolResult.value("success").toBool(true);
    sendResult(id, res);
}

// ── Dispatcher ────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::dispatchTool(const QString &name, const QJsonObject &a)
{
    // Account
    if (name=="github_account_list")          return toolAccountList(a);
    if (name=="github_account_add")           return toolAccountAdd(a);
    if (name=="github_account_remove")        return toolAccountRemove(a);
    // User
    if (name=="github_get_authenticated_user") return toolGetAuthenticatedUser(a);
    if (name=="github_get_user")              return toolGetUser(a);
    if (name=="github_list_user_repos")       return toolListUserRepos(a);
    if (name=="github_list_my_repos")         return toolListAuthenticatedUserRepos(a);
    if (name=="github_get_user_orgs")         return toolGetUserOrgs(a);
    // Orgs
    if (name=="github_get_org")               return toolGetOrg(a);
    if (name=="github_list_org_repos")        return toolListOrgRepos(a);
    if (name=="github_list_org_members")      return toolListOrgMembers(a);
    // Repos
    if (name=="github_repos_list")            return toolReposList(a);
    if (name=="github_repo_get")              return toolRepoGet(a);
    if (name=="github_repo_create")           return toolRepoCreate(a);
    if (name=="github_repo_create_org")       return toolRepoCreateOrg(a);
    if (name=="github_repo_delete")           return toolRepoDelete(a);
    if (name=="github_repo_fork")             return toolRepoFork(a);
    if (name=="github_list_forks")            return toolListForks(a);
    if (name=="github_list_stargazers")       return toolListStargazers(a);
    if (name=="github_list_watchers")         return toolListWatchers(a);
    if (name=="github_star_repo")             return toolStarRepo(a);
    if (name=="github_unstar_repo")           return toolUnstarRepo(a);
    if (name=="github_list_topics")           return toolListTopics(a);
    if (name=="github_list_contributors")     return toolListContributors(a);
    if (name=="github_list_languages")        return toolListLanguages(a);
    // Branches
    if (name=="github_list_branches")         return toolListBranches(a);
    if (name=="github_get_branch")            return toolGetBranch(a);
    if (name=="github_create_branch")         return toolCreateBranch(a);
    if (name=="github_delete_branch")         return toolDeleteBranch(a);
    if (name=="github_get_branch_protection") return toolGetBranchProtection(a);
    // Tags & Commits
    if (name=="github_list_tags")             return toolListTags(a);
    if (name=="github_list_commits")          return toolListCommits(a);
    if (name=="github_get_commit")            return toolGetCommit(a);
    if (name=="github_compare_commits")       return toolCompareCommits(a);
    // Issues
    if (name=="github_issues_list")           return toolIssuesList(a);
    if (name=="github_issue_get")             return toolIssueGet(a);
    if (name=="github_issue_create")          return toolIssueCreate(a);
    if (name=="github_issue_update")          return toolIssueUpdate(a);
    if (name=="github_issue_delete")          return toolIssueClose(a);
    // Issue comments
    if (name=="github_list_issue_comments")   return toolListIssueComments(a);
    if (name=="github_get_issue_comment")     return toolGetIssueComment(a);
    if (name=="github_create_issue_comment")  return toolCreateIssueComment(a);
    if (name=="github_update_issue_comment")  return toolUpdateIssueComment(a);
    if (name=="github_delete_issue_comment")  return toolDeleteIssueComment(a);
    // Labels
    if (name=="github_list_labels")           return toolListLabels(a);
    if (name=="github_get_label")             return toolGetLabel(a);
    if (name=="github_create_label")          return toolCreateLabel(a);
    if (name=="github_update_label")          return toolUpdateLabel(a);
    if (name=="github_delete_label")          return toolDeleteLabel(a);
    // Milestones
    if (name=="github_list_milestones")       return toolListMilestones(a);
    if (name=="github_get_milestone")         return toolGetMilestone(a);
    if (name=="github_create_milestone")      return toolCreateMilestone(a);
    if (name=="github_update_milestone")      return toolUpdateMilestone(a);
    if (name=="github_delete_milestone")      return toolDeleteMilestone(a);
    // PRs
    if (name=="github_prs_list")              return toolPRsList(a);
    if (name=="github_pr_get")                return toolPRGet(a);
    if (name=="github_pr_create")             return toolPRCreate(a);
    if (name=="github_pr_update")             return toolPRUpdate(a);
    if (name=="github_pr_merge")              return toolPRMerge(a);
    if (name=="github_list_pr_comments")      return toolListPRComments(a);
    if (name=="github_create_pr_comment")     return toolCreatePRComment(a);
    if (name=="github_list_pr_reviews")       return toolListPRReviews(a);
    if (name=="github_list_pr_files")         return toolListPRFiles(a);
    // Files
    if (name=="github_file_get")              return toolFileGet(a);
    if (name=="github_file_create")           return toolFileCreate(a);
    if (name=="github_file_update")           return toolFileUpdate(a);
    if (name=="github_file_delete")           return toolFileDelete(a);
    if (name=="github_list_directory")        return toolListDirectory(a);
    // Releases
    if (name=="github_list_releases")         return toolListReleases(a);
    if (name=="github_get_release")           return toolGetRelease(a);
    if (name=="github_get_latest_release")    return toolGetLatestRelease(a);
    if (name=="github_get_release_by_tag")    return toolGetReleaseByTag(a);
    if (name=="github_create_release")        return toolCreateRelease(a);
    if (name=="github_update_release")        return toolUpdateRelease(a);
    if (name=="github_delete_release")        return toolDeleteRelease(a);
    // Webhooks
    if (name=="github_list_webhooks")         return toolListWebhooks(a);
    if (name=="github_get_webhook")           return toolGetWebhook(a);
    if (name=="github_create_webhook")        return toolCreateWebhook(a);
    if (name=="github_update_webhook")        return toolUpdateWebhook(a);
    if (name=="github_delete_webhook")        return toolDeleteWebhook(a);
    if (name=="github_ping_webhook")          return toolPingWebhook(a);
    // Actions
    if (name=="github_list_workflows")        return toolListWorkflows(a);
    if (name=="github_get_workflow")          return toolGetWorkflow(a);
    if (name=="github_list_workflow_runs")    return toolListWorkflowRuns(a);
    if (name=="github_get_workflow_run")      return toolGetWorkflowRun(a);
    if (name=="github_cancel_workflow_run")   return toolCancelWorkflowRun(a);
    if (name=="github_rerun_workflow")        return toolRerunWorkflow(a);
    if (name=="github_list_run_artifacts")    return toolListRunArtifacts(a);
    // Gists
    if (name=="github_list_gists")            return toolListGists(a);
    if (name=="github_get_gist")              return toolGetGist(a);
    if (name=="github_create_gist")           return toolCreateGist(a);
    if (name=="github_update_gist")           return toolUpdateGist(a);
    if (name=="github_delete_gist")           return toolDeleteGist(a);
    if (name=="github_list_gist_comments")    return toolListGistComments(a);
    if (name=="github_create_gist_comment")   return toolCreateGistComment(a);
    if (name=="github_delete_gist_comment")   return toolDeleteGistComment(a);
    // Search
    if (name=="github_search_code")           return toolSearchCode(a);
    if (name=="github_search_repos")          return toolSearchRepos(a);
    if (name=="github_search_issues")         return toolSearchIssues(a);
    if (name=="github_search_users")          return toolSearchUsers(a);
    if (name=="github_search_commits")        return toolSearchCommits(a);
    // Rate limit
    if (name=="github_get_rate_limit")        return toolGetRateLimit(a);

    // Logging
    if (name=="github_set_logging")           return toolSetLogging(a);
    if (name=="github_get_logging_status")    return toolGetLoggingStatus(a);

    // Cache
    if (name=="github_set_cache")             return toolSetCache(a);
    if (name=="github_clear_cache")           return toolClearCache(a);
    if (name=="github_get_cache_stats")       return toolGetCacheStats(a);
    if (name=="github_invalidate_cache")      return toolInvalidateCache(a);

    // Pagination
    if (name=="github_set_pagination")        return toolSetPagination(a);

    // Project upload
    if (name=="github_upload_project")        return toolUploadProject(a);

    // Git sync
    if (name=="github_sync_repository")       return toolSyncRepository(a);

    QJsonObject r; r["success"]=false; r["error"]="Unknown tool: "+name; return r;
}

// ── Account ───────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolAccountList(const QJsonObject &)
{
    QJsonArray accounts;
    for (const auto &acc : m_accounts) {
        QJsonObject a;
        a["name"]        = acc.name();
        a["username"]    = acc.username();
        a["type"]        = GitHubAccount::typeToString(acc.type());
        a["baseUrl"]     = acc.baseUrl();
        a["apiBaseUrl"]  = acc.apiBaseUrl();
        a["enterprise"]  = acc.isEnterprise();
        accounts.append(a);
    }
    QJsonObject r; r["success"] = true; r["accounts"] = accounts; return r;
}

QJsonObject GitHubMCPServer::toolAccountAdd(const QJsonObject &args)
{
    QString name     = args.value("name").toString();
    QString token    = args.value("token").toString();
    QString username = args.value("username").toString();
    QString baseUrl  = args.value("base_url").toString("https://api.github.com");
    QString typeStr  = args.value("type").toString("github");
    if (name.isEmpty() || token.isEmpty()) {
        QJsonObject r; r["success"]=false; r["error"]="name and token are required"; return r;
    }
    GitHubAccount::Type accType = GitHubAccount::typeFromString(typeStr);
    GitHubAccount acc(name, token, username, baseUrl, accType);
    m_accounts[name] = acc;
    if (m_apis.contains(name)) { delete m_apis[name]; m_apis.remove(name); }
    saveConfig();
    QJsonObject r; r["success"]=true;
    r["message"]=QString("Account '%1' added (type: %2)").arg(name, GitHubAccount::typeToString(accType));
    return r;
}

QJsonObject GitHubMCPServer::toolAccountRemove(const QJsonObject &args)
{
    QString name = args.value("name").toString();
    if (!m_accounts.contains(name)) {
        QJsonObject r; r["success"]=false; r["error"]=QString("Account '%1' not found").arg(name); return r;
    }
    m_accounts.remove(name);
    if (m_apis.contains(name)) { delete m_apis[name]; m_apis.remove(name); }
    saveConfig();
    QJsonObject r; r["success"]=true; r["message"]=QString("Account '%1' removed").arg(name); return r;
}

// ── User ──────────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolGetAuthenticatedUser(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getAuthenticatedUser()); }

QJsonObject GitHubMCPServer::toolGetUser(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getUser(args.value("username").toString())); }

QJsonObject GitHubMCPServer::toolListUserRepos(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listUserRepos(args.value("username").toString(),
    args.value("type").toString("all"), args.value("per_page").toInt(30), args.value("page").toInt(1))); }

QJsonObject GitHubMCPServer::toolListAuthenticatedUserRepos(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listAuthenticatedUserRepos(
    args.value("type").toString("all"), args.value("per_page").toInt(30), args.value("page").toInt(1))); }

QJsonObject GitHubMCPServer::toolGetUserOrgs(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getUserOrgs(args.value("username").toString())); }

// ── Organizations ─────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolGetOrg(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getOrg(args.value("org").toString())); }

QJsonObject GitHubMCPServer::toolListOrgRepos(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listOrgRepos(args.value("org").toString(),
    args.value("type").toString("all"), args.value("per_page").toInt(30), args.value("page").toInt(1))); }

QJsonObject GitHubMCPServer::toolListOrgMembers(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listOrgMembers(args.value("org").toString())); }

// ── Repos ─────────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolReposList(const QJsonObject &args)
{
    REQUIRE_API;
    QString owner = args.value("owner").toString();
    if (owner.isEmpty()) owner = api->account().username();
    return apiResponseToJson(api->listUserRepos(owner, args.value("type").toString("all"),
                                                args.value("per_page").toInt(30),
                                                args.value("page").toInt(1)));
}

QJsonObject GitHubMCPServer::toolRepoGet(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getRepo(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolRepoCreate(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createRepo(args.value("name").toString(),
    args.value("description").toString(), args.value("private").toBool(false))); }

QJsonObject GitHubMCPServer::toolRepoCreateOrg(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createOrgRepo(args.value("org").toString(),
    args.value("name").toString(), args.value("description").toString(), args.value("private").toBool(false))); }

QJsonObject GitHubMCPServer::toolRepoDelete(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->deleteRepo(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolRepoFork(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->forkRepo(args.value("owner").toString(),
    args.value("repo").toString(), args.value("org").toString())); }

QJsonObject GitHubMCPServer::toolListForks(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listForks(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolListStargazers(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listStargazers(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolListWatchers(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listWatchers(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolStarRepo(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->starRepo(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolUnstarRepo(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->unstarRepo(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolListTopics(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listTopics(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolListContributors(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listContributors(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolListLanguages(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listLanguages(args.value("owner").toString(), args.value("repo").toString())); }

// ── Branches ──────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolListBranches(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listBranches(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolGetBranch(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getBranch(args.value("owner").toString(),
    args.value("repo").toString(), args.value("branch").toString())); }

QJsonObject GitHubMCPServer::toolCreateBranch(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createBranch(args.value("owner").toString(),
    args.value("repo").toString(), args.value("branch").toString(), args.value("sha").toString())); }

QJsonObject GitHubMCPServer::toolDeleteBranch(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->deleteBranch(args.value("owner").toString(),
    args.value("repo").toString(), args.value("branch").toString())); }

QJsonObject GitHubMCPServer::toolGetBranchProtection(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getBranchProtection(args.value("owner").toString(),
    args.value("repo").toString(), args.value("branch").toString())); }

// ── Tags & Commits ────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolListTags(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listTags(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolListCommits(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listCommits(args.value("owner").toString(),
    args.value("repo").toString(), args.value("sha").toString(), args.value("path").toString(),
    args.value("author").toString(), args.value("per_page").toInt(30), args.value("page").toInt(1))); }

QJsonObject GitHubMCPServer::toolGetCommit(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getCommit(args.value("owner").toString(),
    args.value("repo").toString(), args.value("sha").toString())); }

QJsonObject GitHubMCPServer::toolCompareCommits(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->compareCommits(args.value("owner").toString(),
    args.value("repo").toString(), args.value("base").toString(), args.value("head").toString())); }

// ── Issues ────────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolIssuesList(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listIssues(args.value("owner").toString(),
    args.value("repo").toString(), args.value("state").toString("open"),
    args.value("labels").toString(), args.value("per_page").toInt(30), args.value("page").toInt(1))); }

QJsonObject GitHubMCPServer::toolIssueGet(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getIssue(args.value("owner").toString(),
    args.value("repo").toString(), args.value("issue_number").toInt())); }

QJsonObject GitHubMCPServer::toolIssueCreate(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createIssue(args.value("owner").toString(),
    args.value("repo").toString(), args.value("title").toString(), args.value("body").toString(),
    args.value("labels").toString(), args.value("milestone").toInt(0),
    args.value("assignees").toString())); }

QJsonObject GitHubMCPServer::toolIssueUpdate(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->updateIssue(args.value("owner").toString(),
    args.value("repo").toString(), args.value("issue_number").toInt(),
    args.value("state").toString(), args.value("title").toString(), args.value("body").toString())); }

QJsonObject GitHubMCPServer::toolIssueClose(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->closeIssue(args.value("owner").toString(),
    args.value("repo").toString(), args.value("issue_number").toInt())); }

// ── Issue Comments ────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolListIssueComments(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listIssueComments(args.value("owner").toString(),
    args.value("repo").toString(), args.value("issue_number").toInt())); }

QJsonObject GitHubMCPServer::toolGetIssueComment(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getIssueComment(args.value("owner").toString(),
    args.value("repo").toString(), args.value("comment_id").toInt())); }

QJsonObject GitHubMCPServer::toolCreateIssueComment(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createIssueComment(args.value("owner").toString(),
    args.value("repo").toString(), args.value("issue_number").toInt(), args.value("body").toString())); }

QJsonObject GitHubMCPServer::toolUpdateIssueComment(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->updateIssueComment(args.value("owner").toString(),
    args.value("repo").toString(), args.value("comment_id").toInt(), args.value("body").toString())); }

QJsonObject GitHubMCPServer::toolDeleteIssueComment(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->deleteIssueComment(args.value("owner").toString(),
    args.value("repo").toString(), args.value("comment_id").toInt())); }

// ── Labels ────────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolListLabels(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listLabels(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolGetLabel(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getLabel(args.value("owner").toString(),
    args.value("repo").toString(), args.value("name").toString())); }

QJsonObject GitHubMCPServer::toolCreateLabel(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createLabel(args.value("owner").toString(),
    args.value("repo").toString(), args.value("name").toString(),
    args.value("color").toString(), args.value("description").toString())); }

QJsonObject GitHubMCPServer::toolUpdateLabel(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->updateLabel(args.value("owner").toString(),
    args.value("repo").toString(), args.value("name").toString(), args.value("new_name").toString(),
    args.value("color").toString(), args.value("description").toString())); }

QJsonObject GitHubMCPServer::toolDeleteLabel(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->deleteLabel(args.value("owner").toString(),
    args.value("repo").toString(), args.value("name").toString())); }

// ── Milestones ────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolListMilestones(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listMilestones(args.value("owner").toString(),
    args.value("repo").toString(), args.value("state").toString("open"))); }

QJsonObject GitHubMCPServer::toolGetMilestone(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getMilestone(args.value("owner").toString(),
    args.value("repo").toString(), args.value("milestone_number").toInt())); }

QJsonObject GitHubMCPServer::toolCreateMilestone(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createMilestone(args.value("owner").toString(),
    args.value("repo").toString(), args.value("title").toString(),
    args.value("description").toString(), args.value("due_on").toString())); }

QJsonObject GitHubMCPServer::toolUpdateMilestone(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->updateMilestone(args.value("owner").toString(),
    args.value("repo").toString(), args.value("milestone_number").toInt(),
    args.value("title").toString(), args.value("description").toString(),
    args.value("state").toString(), args.value("due_on").toString())); }

QJsonObject GitHubMCPServer::toolDeleteMilestone(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->deleteMilestone(args.value("owner").toString(),
    args.value("repo").toString(), args.value("milestone_number").toInt())); }

// ── Pull Requests ─────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolPRsList(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listPRs(args.value("owner").toString(),
    args.value("repo").toString(), args.value("state").toString("open"),
    args.value("per_page").toInt(30), args.value("page").toInt(1))); }

QJsonObject GitHubMCPServer::toolPRGet(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getPR(args.value("owner").toString(),
    args.value("repo").toString(), args.value("pr_number").toInt())); }

QJsonObject GitHubMCPServer::toolPRCreate(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createPR(args.value("owner").toString(),
    args.value("repo").toString(), args.value("title").toString(), args.value("head").toString(),
    args.value("base").toString(), args.value("body").toString())); }

QJsonObject GitHubMCPServer::toolPRUpdate(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->updatePR(args.value("owner").toString(),
    args.value("repo").toString(), args.value("pr_number").toInt(),
    args.value("title").toString(), args.value("body").toString(), args.value("state").toString())); }

QJsonObject GitHubMCPServer::toolPRMerge(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->mergePR(args.value("owner").toString(),
    args.value("repo").toString(), args.value("pr_number").toInt(),
    args.value("merge_method").toString("merge"))); }

QJsonObject GitHubMCPServer::toolListPRComments(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listPRComments(args.value("owner").toString(),
    args.value("repo").toString(), args.value("pr_number").toInt())); }

QJsonObject GitHubMCPServer::toolCreatePRComment(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createPRComment(args.value("owner").toString(),
    args.value("repo").toString(), args.value("pr_number").toInt(), args.value("body").toString())); }

QJsonObject GitHubMCPServer::toolListPRReviews(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listPRReviews(args.value("owner").toString(),
    args.value("repo").toString(), args.value("pr_number").toInt())); }

QJsonObject GitHubMCPServer::toolListPRFiles(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listPRFiles(args.value("owner").toString(),
    args.value("repo").toString(), args.value("pr_number").toInt())); }

// ── Files ─────────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolFileGet(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getFile(args.value("owner").toString(),
    args.value("repo").toString(), args.value("path").toString(), args.value("ref").toString())); }

QJsonObject GitHubMCPServer::toolFileCreate(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createFile(args.value("owner").toString(),
    args.value("repo").toString(), args.value("path").toString(), args.value("message").toString(),
    args.value("content").toString(), args.value("branch").toString())); }

QJsonObject GitHubMCPServer::toolFileUpdate(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->updateFile(args.value("owner").toString(),
    args.value("repo").toString(), args.value("path").toString(), args.value("message").toString(),
    args.value("content").toString(), args.value("sha").toString(), args.value("branch").toString())); }

QJsonObject GitHubMCPServer::toolFileDelete(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->deleteFile(args.value("owner").toString(),
    args.value("repo").toString(), args.value("path").toString(), args.value("message").toString(),
    args.value("sha").toString(), args.value("branch").toString())); }

QJsonObject GitHubMCPServer::toolListDirectory(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listDirectory(args.value("owner").toString(),
    args.value("repo").toString(), args.value("path").toString(""), args.value("ref").toString())); }

// ── Releases ──────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolListReleases(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listReleases(args.value("owner").toString(),
    args.value("repo").toString(), args.value("per_page").toInt(30))); }

QJsonObject GitHubMCPServer::toolGetRelease(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getRelease(args.value("owner").toString(),
    args.value("repo").toString(), args.value("release_id").toInt())); }

QJsonObject GitHubMCPServer::toolGetLatestRelease(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getLatestRelease(args.value("owner").toString(),
    args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolGetReleaseByTag(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getReleaseByTag(args.value("owner").toString(),
    args.value("repo").toString(), args.value("tag").toString())); }

QJsonObject GitHubMCPServer::toolCreateRelease(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createRelease(args.value("owner").toString(),
    args.value("repo").toString(), args.value("tag_name").toString(), args.value("name").toString(),
    args.value("body").toString(), args.value("draft").toBool(false),
    args.value("prerelease").toBool(false), args.value("target_commitish").toString())); }

QJsonObject GitHubMCPServer::toolUpdateRelease(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->updateRelease(args.value("owner").toString(),
    args.value("repo").toString(), args.value("release_id").toInt(),
    args.value("tag_name").toString(), args.value("name").toString(), args.value("body").toString(),
    args.value("draft").toBool(false), args.value("prerelease").toBool(false))); }

QJsonObject GitHubMCPServer::toolDeleteRelease(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->deleteRelease(args.value("owner").toString(),
    args.value("repo").toString(), args.value("release_id").toInt())); }

// ── Webhooks ──────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolListWebhooks(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listWebhooks(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolGetWebhook(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getWebhook(args.value("owner").toString(),
    args.value("repo").toString(), args.value("hook_id").toInt())); }

QJsonObject GitHubMCPServer::toolCreateWebhook(const QJsonObject &args)
{
    REQUIRE_API;
    QStringList events;
    for (const QJsonValue &v : args.value("events").toArray()) events.append(v.toString());
    return apiResponseToJson(api->createWebhook(args.value("owner").toString(),
        args.value("repo").toString(), args.value("url").toString(), events,
        args.value("content_type").toString("json"), args.value("active").toBool(true)));
}

QJsonObject GitHubMCPServer::toolUpdateWebhook(const QJsonObject &args)
{
    REQUIRE_API;
    QStringList events;
    for (const QJsonValue &v : args.value("events").toArray()) events.append(v.toString());
    return apiResponseToJson(api->updateWebhook(args.value("owner").toString(),
        args.value("repo").toString(), args.value("hook_id").toInt(),
        args.value("url").toString(), events, args.value("active").toBool(true)));
}

QJsonObject GitHubMCPServer::toolDeleteWebhook(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->deleteWebhook(args.value("owner").toString(),
    args.value("repo").toString(), args.value("hook_id").toInt())); }

QJsonObject GitHubMCPServer::toolPingWebhook(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->pingWebhook(args.value("owner").toString(),
    args.value("repo").toString(), args.value("hook_id").toInt())); }

// ── Actions ───────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolListWorkflows(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listWorkflows(args.value("owner").toString(), args.value("repo").toString())); }

QJsonObject GitHubMCPServer::toolGetWorkflow(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getWorkflow(args.value("owner").toString(),
    args.value("repo").toString(), args.value("workflow_id").toString())); }

QJsonObject GitHubMCPServer::toolListWorkflowRuns(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listWorkflowRuns(args.value("owner").toString(),
    args.value("repo").toString(), args.value("workflow_id").toString(),
    args.value("status").toString(), args.value("per_page").toInt(30))); }

QJsonObject GitHubMCPServer::toolGetWorkflowRun(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getWorkflowRun(args.value("owner").toString(),
    args.value("repo").toString(), args.value("run_id").toInt())); }

QJsonObject GitHubMCPServer::toolCancelWorkflowRun(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->cancelWorkflowRun(args.value("owner").toString(),
    args.value("repo").toString(), args.value("run_id").toInt())); }

QJsonObject GitHubMCPServer::toolRerunWorkflow(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->rerunWorkflow(args.value("owner").toString(),
    args.value("repo").toString(), args.value("run_id").toInt())); }

QJsonObject GitHubMCPServer::toolListRunArtifacts(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listRunArtifacts(args.value("owner").toString(),
    args.value("repo").toString(), args.value("run_id").toInt())); }

// ── Gists ─────────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolListGists(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listGists(args.value("per_page").toInt(30))); }

QJsonObject GitHubMCPServer::toolGetGist(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getGist(args.value("gist_id").toString())); }

QJsonObject GitHubMCPServer::toolCreateGist(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createGist(args.value("description").toString(),
    args.value("public").toBool(false), args.value("filename").toString(),
    args.value("content").toString())); }

QJsonObject GitHubMCPServer::toolUpdateGist(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->updateGist(args.value("gist_id").toString(),
    args.value("description").toString(), args.value("filename").toString(),
    args.value("content").toString())); }

QJsonObject GitHubMCPServer::toolDeleteGist(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->deleteGist(args.value("gist_id").toString())); }

QJsonObject GitHubMCPServer::toolListGistComments(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->listGistComments(args.value("gist_id").toString())); }

QJsonObject GitHubMCPServer::toolCreateGistComment(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->createGistComment(args.value("gist_id").toString(),
    args.value("body").toString())); }

QJsonObject GitHubMCPServer::toolDeleteGistComment(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->deleteGistComment(args.value("gist_id").toString(),
    args.value("comment_id").toInt())); }

// ── Search ────────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolSearchCode(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->searchCode(args.value("query").toString(), args.value("per_page").toInt(30))); }

QJsonObject GitHubMCPServer::toolSearchRepos(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->searchRepos(args.value("query").toString(), args.value("per_page").toInt(30))); }

QJsonObject GitHubMCPServer::toolSearchIssues(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->searchIssues(args.value("query").toString(), args.value("per_page").toInt(30))); }

QJsonObject GitHubMCPServer::toolSearchUsers(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->searchUsers(args.value("query").toString(), args.value("per_page").toInt(30))); }

QJsonObject GitHubMCPServer::toolSearchCommits(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->searchCommits(args.value("query").toString(), args.value("per_page").toInt(30))); }

// ── Rate limit ────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolGetRateLimit(const QJsonObject &args)
{ REQUIRE_API; return apiResponseToJson(api->getRateLimit()); }

// ── Logging ───────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolSetLogging(const QJsonObject &args)
{
    QString levelStr = args.value("level").toString("none");
    QString logFile  = args.value("log_file").toString();
    int     maxBody  = args.value("max_body_bytes").toInt(4096);

    TrafficLogger::LogLevel level = TrafficLogger::LogLevel::None;
    if      (levelStr == "summary") level = TrafficLogger::LogLevel::Summary;
    else if (levelStr == "headers") level = TrafficLogger::LogLevel::Headers;
    else if (levelStr == "full")    level = TrafficLogger::LogLevel::Full;

    gTrafficLogger.setLogLevel(level);
    gTrafficLogger.setMaxBodyBytes(maxBody);
    if (!logFile.isEmpty())
        gTrafficLogger.setLogFile(logFile);

    QJsonObject r; r["success"]=true;
    r["level"]          = levelStr;
    r["log_file"]       = logFile.isEmpty() ? "(not set)" : logFile;
    r["max_body_bytes"] = maxBody;
    return r;
}

QJsonObject GitHubMCPServer::toolGetLoggingStatus(const QJsonObject &)
{
    QJsonObject r; r["success"]=true;
    switch (gTrafficLogger.logLevel()) {
    case TrafficLogger::LogLevel::None:    r["level"]="none";    break;
    case TrafficLogger::LogLevel::Summary: r["level"]="summary"; break;
    case TrafficLogger::LogLevel::Headers: r["level"]="headers"; break;
    case TrafficLogger::LogLevel::Full:    r["level"]="full";    break;
    }
    return r;
}

// ── Cache ─────────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolSetCache(const QJsonObject &args)
{
    bool enabled    = args.value("enabled").toBool(false);
    int  ttl        = args.value("ttl_seconds").toInt(60);
    int  maxEntries = args.value("max_entries").toInt(512);

    gResponseCache.setEnabled(enabled);
    gResponseCache.setDefaultTtl(ttl);
    gResponseCache.setMaxEntries(maxEntries);

    QJsonObject r; r["success"]=true;
    r["enabled"]     = enabled;
    r["ttl_seconds"] = ttl;
    r["max_entries"] = maxEntries;
    return r;
}

QJsonObject GitHubMCPServer::toolClearCache(const QJsonObject &)
{
    gResponseCache.clear();
    QJsonObject r; r["success"]=true; r["message"]="Cache cleared"; return r;
}

QJsonObject GitHubMCPServer::toolGetCacheStats(const QJsonObject &)
{
    QJsonObject r; r["success"]=true;
    r["enabled"] = gResponseCache.isEnabled();
    r["size"]    = gResponseCache.size();
    r["hits"]    = gResponseCache.hits();
    r["misses"]  = gResponseCache.misses();
    int total    = gResponseCache.hits() + gResponseCache.misses();
    r["hit_rate"] = total > 0
        ? QString::number(100.0 * gResponseCache.hits() / total, 'f', 1) + "%"
        : "N/A";
    return r;
}

QJsonObject GitHubMCPServer::toolInvalidateCache(const QJsonObject &args)
{
    QString owner = args.value("owner").toString();
    QString repo  = args.value("repo").toString();
    if (owner.isEmpty()) {
        QJsonObject r; r["success"]=false; r["error"]="owner is required"; return r;
    }
    GitHubAPI *api = apiForAccount(args.value("account").toString());
    if (api) api->invalidateCacheFor(owner, repo);
    QJsonObject r; r["success"]=true;
    r["message"] = QString("Cache invalidated for %1%2")
                       .arg(owner, repo.isEmpty() ? "" : "/" + repo);
    return r;
}

// ── Pagination ────────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolSetPagination(const QJsonObject &args)
{
    bool autoFetch = args.value("auto_fetch_all").toBool(false);
    int  maxPages  = args.value("max_pages").toInt(100);

    for (GitHubAPI *api : m_apis) {
        api->setAutoFetchAllPages(autoFetch);
        api->setMaxPages(maxPages);
    }
    m_defaultAutoFetch = autoFetch;
    m_defaultMaxPages  = maxPages;

    QJsonObject r; r["success"]=true;
    r["auto_fetch_all"] = autoFetch;
    r["max_pages"]      = maxPages;
    r["note"] = autoFetch
        ? QString("Will auto-fetch all pages (up to %1) for list operations").arg(maxPages)
        : "Manual pagination: use 'page' parameter to navigate";
    return r;
}

// ── Git repository sync ───────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolSyncRepository(const QJsonObject &args)
{
    REQUIRE_API;

    QString localPath  = args.value("local_path").toString();
    QString owner      = args.value("owner").toString();
    QString repo       = args.value("repo").toString();
    QString branch     = args.value("branch").toString("main");
    QString remote     = args.value("remote").toString("origin");
    QString commitMsg  = args.value("commit_message").toString("Sync");
    bool    forcePush  = args.value("force_push").toBool(false);

    if (localPath.isEmpty() || owner.isEmpty() || repo.isEmpty()) {
        QJsonObject r;
        r["success"] = false;
        r["error"]   = "local_path, owner and repo are required";
        return r;
    }

    GitHubAPI::SyncResult res = api->syncRepository(localPath, owner, repo,
                                                      branch, remote, commitMsg, forcePush);

    QJsonArray stepsArr;
    for (const auto &s : res.steps) {
        QJsonObject so;
        so["step"]   = s.name;
        so["ok"]     = s.ok;
        so["output"] = s.output;
        stepsArr.append(so);
    }

    QJsonObject r;
    r["success"] = res.success;
    r["steps"]   = stepsArr;
    r["output"]  = res.output;
    if (!res.errorMessage.isEmpty())
        r["error"] = res.errorMessage;
    return r;
}

// ── Project upload ────────────────────────────────────────────────────────────

QJsonObject GitHubMCPServer::toolUploadProject(const QJsonObject &args)
{
    REQUIRE_API;

    QString localPath     = args.value("local_path").toString();
    QString owner         = args.value("owner").toString();
    QString repo          = args.value("repo").toString();
    QString branch        = args.value("branch").toString("main");
    QString commitPrefix  = args.value("commit_message").toString("Upload");

    if (localPath.isEmpty() || owner.isEmpty() || repo.isEmpty()) {
        QJsonObject r;
        r["success"] = false;
        r["error"]   = "local_path, owner and repo are required";
        return r;
    }

    QStringList ignorePatterns;
    for (const QJsonValue &v : args.value("ignore").toArray())
        ignorePatterns.append(v.toString());

    GitHubAPI::UploadResult res = api->uploadProject(localPath, owner, repo,
                                                      branch, commitPrefix, ignorePatterns);
    QJsonObject r;
    r["success"]    = res.success;
    r["uploaded"]   = res.uploaded;
    r["skipped"]    = res.skipped;
    r["failed"]     = res.failed;
    r["log"]        = res.log;
    if (!res.errorMessage.isEmpty())
        r["error"] = res.errorMessage;
    return r;
}

// ── Config ────────────────────────────────────────────────────────────────────

bool GitHubMCPServer::loadConfig()
{
    QFile f(m_configPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) return false;
    m_accounts.clear();
    for (const QJsonValue &v : doc.object().value("accounts").toArray()) {
        GitHubAccount acc = GitHubAccount::fromJson(v.toObject());
        if (acc.isValid()) m_accounts[acc.name()] = acc;
    }
    return true;
}

bool GitHubMCPServer::saveConfig()
{
    QJsonArray accounts;
    for (const auto &acc : m_accounts) accounts.append(acc.toJson());
    QJsonObject root; root["accounts"] = accounts;
    QFile f(m_configPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void GitHubMCPServer::createDefaultConfig()
{
    QJsonObject ex;
    ex["name"]     = "example";
    ex["type"]     = "github";
    ex["username"] = "your_github_login";
    ex["token"]    = "ghp_your_token_here";
    ex["baseUrl"]  = "https://api.github.com";
    QJsonArray accounts; accounts.append(ex);
    QJsonObject root; root["accounts"] = accounts;
    QFile f(m_configPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

// ── Helpers ───────────────────────────────────────────────────────────────────

GitHubAPI *GitHubMCPServer::apiForAccount(const QString &name)
{
    QString key = name;
    if (key.isEmpty() || !m_accounts.contains(key)) {
        if (m_accounts.isEmpty()) return nullptr;
        key = m_accounts.firstKey();
    }
    if (!m_apis.contains(key)) {
        GitHubAPI *api = new GitHubAPI(this);
        api->setAccount(m_accounts[key]);
        api->setAutoFetchAllPages(m_defaultAutoFetch);
        api->setMaxPages(m_defaultMaxPages);
        m_apis[key] = api;
    }
    return m_apis[key];
}

QJsonObject GitHubMCPServer::apiResponseToJson(const ApiResponse &resp)
{
    QJsonObject obj;
    obj["success"]  = resp.success;
    obj["httpCode"] = resp.httpCode;
    if (!resp.success) obj["error"] = resp.errorMessage;
    obj["data"] = resp.isArray ? QJsonValue(resp.array) : QJsonValue(resp.data);
    return obj;
}

void GitHubMCPServer::sendResult(const QJsonValue &id, const QJsonValue &result)
{
    QJsonObject r; r["jsonrpc"]="2.0"; r["id"]=id; r["result"]=result; sendResponse(r);
}

void GitHubMCPServer::sendError(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject e; e["code"]=code; e["message"]=message;
    QJsonObject r; r["jsonrpc"]="2.0"; r["id"]=id; r["error"]=e; sendResponse(r);
}

void GitHubMCPServer::sendResponse(const QJsonObject &obj)
{
    QString line = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    debugLog(">>>> " + line);
    m_out << line << "\n"; m_out.flush();
}

void GitHubMCPServer::debugLog(const QString &msg)
{
    if (m_debug) {
        m_err << "[" << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "] " << msg << "\n";
        m_err.flush();
    }
}

// ── Tools list ────────────────────────────────────────────────────────────────

static QJsonObject makeSchema(const QStringList &required, const QJsonObject &props)
{
    QJsonObject s; s["type"]="object"; s["properties"]=props;
    if (!required.isEmpty()) {
        QJsonArray a; for (const QString &r : required) a.append(r); s["required"]=a;
    }
    return s;
}
static QJsonObject S(const QString &d) { QJsonObject p; p["type"]="string"; p["description"]=d; return p; }
static QJsonObject I(const QString &d) { QJsonObject p; p["type"]="integer"; p["description"]=d; return p; }
static QJsonObject B(const QString &d) { QJsonObject p; p["type"]="boolean"; p["description"]=d; return p; }
static QJsonObject A(const QString &d) { QJsonObject p; p["type"]="array"; p["description"]=d;
    QJsonObject items; items["type"]="string"; p["items"]=items; return p; }

QJsonArray GitHubMCPServer::buildToolsList() const
{
    QJsonArray tools;
    auto add = [&](const QString &name, const QString &desc,
                   const QStringList &req, const QJsonObject &props) {
        QJsonObject t; t["name"]=name; t["description"]=desc; t["inputSchema"]=makeSchema(req,props);
        tools.append(t);
    };

    QJsonObject acct{{"account", S("Account name (optional, uses first if omitted)")}};

    // Account management
    add("github_account_list",   "List configured accounts", {}, {});
    add("github_account_add",    "Add a GitHub or GitHub Enterprise account",
        {"name","token"},
        QJsonObject{{"name",S("Account alias")},{"token",S("GitHub PAT")},
                    {"username",S("GitHub login (e.g. octocat)")},
                    {"type",S("Account type: 'github' (default) or 'github_enterprise'")},
                    {"base_url",S("API base URL. For GHE: https://github.mycompany.com (or https://github.mycompany.com/api/v3)")}}); 
    add("github_account_remove", "Remove an account", {"name"},
        QJsonObject{{"name",S("Account alias")}});

    // User
    add("github_get_authenticated_user","Get the authenticated user",{},acct);
    add("github_get_user","Get a user by username",{"username"},
        QJsonObject{{"username",S("GitHub login")},{"account",S("Account (optional)")}});
    add("github_list_user_repos","List repos for a user",{"username"},
        QJsonObject{{"username",S("Login")},{"type",S("all/owner/member")},
                    {"per_page",I("Per page (max 100)")},{"page",I("Page number")},{"account",S("Account (optional)")}});
    add("github_list_my_repos","List authenticated user's repos",{},
        QJsonObject{{"type",S("all/owner/member/public/private")},
                    {"per_page",I("Per page")},{"page",I("Page")},{"account",S("Account (optional)")}});
    add("github_get_user_orgs","List organizations for a user",{"username"},
        QJsonObject{{"username",S("Login")},{"account",S("Account (optional)")}});

    // Orgs
    add("github_get_org","Get organization info",{"org"},
        QJsonObject{{"org",S("Org login")},{"account",S("Account (optional)")}});
    add("github_list_org_repos","List org repositories",{"org"},
        QJsonObject{{"org",S("Org login")},{"type",S("all/public/private/forks/sources/member")},
                    {"per_page",I("Per page")},{"page",I("Page")},{"account",S("Account (optional)")}});
    add("github_list_org_members","List org members",{"org"},
        QJsonObject{{"org",S("Org login")},{"account",S("Account (optional)")}});

    // Repos
    add("github_repos_list","List repos for owner (uses account username if omitted)",{},
        QJsonObject{{"owner",S("Owner (optional, falls back to account username)")},{"type",S("all/owner/member")},
                    {"per_page",I("Per page")},{"page",I("Page")},{"account",S("Account (optional)")}});
    add("github_repo_get","Get repository details",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo name")},{"account",S("Account (optional)")}});
    add("github_repo_create","Create user repository",{"name"},
        QJsonObject{{"name",S("Name")},{"description",S("Description")},{"private",B("Private?")},{"account",S("Account (optional)")}});
    add("github_repo_create_org","Create org repository",{"org","name"},
        QJsonObject{{"org",S("Org login")},{"name",S("Name")},{"description",S("Description")},{"private",B("Private?")},{"account",S("Account (optional)")}});
    add("github_repo_delete","Delete repository",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_repo_fork","Fork repository",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"org",S("Fork into org (optional)")},{"account",S("Account (optional)")}});
    add("github_list_forks","List repository forks",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_list_stargazers","List stargazers",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_list_watchers","List watchers",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_star_repo","Star a repository",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_unstar_repo","Unstar a repository",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_list_topics","List repository topics",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_list_contributors","List contributors",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_list_languages","List repository languages",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});

    // Branches
    add("github_list_branches","List branches",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_get_branch","Get a branch",{"owner","repo","branch"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"branch",S("Branch name")},{"account",S("Account (optional)")}});
    add("github_create_branch","Create a branch from SHA",{"owner","repo","branch","sha"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"branch",S("New branch name")},{"sha",S("Source commit SHA")},{"account",S("Account (optional)")}});
    add("github_delete_branch","Delete a branch",{"owner","repo","branch"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"branch",S("Branch name")},{"account",S("Account (optional)")}});
    add("github_get_branch_protection","Get branch protection rules",{"owner","repo","branch"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"branch",S("Branch name")},{"account",S("Account (optional)")}});

    // Tags & Commits
    add("github_list_tags","List tags",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_list_commits","List commits",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"sha",S("Branch/tag/SHA (optional)")},
                    {"path",S("Filter by file path (optional)")},{"author",S("Filter by author (optional)")},
                    {"per_page",I("Per page")},{"page",I("Page")},{"account",S("Account (optional)")}});
    add("github_get_commit","Get a commit",{"owner","repo","sha"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"sha",S("Commit SHA")},{"account",S("Account (optional)")}});
    add("github_compare_commits","Compare two commits/branches",{"owner","repo","base","head"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"base",S("Base ref")},{"head",S("Head ref")},{"account",S("Account (optional)")}});

    // Issues
    add("github_issues_list","List issues",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"state",S("open/closed/all")},
                    {"labels",S("Comma-separated labels")},{"per_page",I("Per page")},{"page",I("Page")},{"account",S("Account (optional)")}});
    add("github_issue_get","Get an issue",{"owner","repo","issue_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"issue_number",I("Issue number")},{"account",S("Account (optional)")}});
    add("github_issue_create","Create an issue",{"owner","repo","title"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"title",S("Title")},{"body",S("Body")},
                    {"labels",S("Comma-separated labels")},{"milestone",I("Milestone number")},
                    {"assignees",S("Comma-separated assignees")},{"account",S("Account (optional)")}});
    add("github_issue_update","Update an issue",{"owner","repo","issue_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"issue_number",I("Issue number")},
                    {"state",S("open/closed")},{"title",S("New title")},{"body",S("New body")},{"account",S("Account (optional)")}});
    add("github_issue_delete","Close an issue",{"owner","repo","issue_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"issue_number",I("Issue number")},{"account",S("Account (optional)")}});

    // Issue Comments
    add("github_list_issue_comments","List issue comments",{"owner","repo","issue_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"issue_number",I("Issue number")},{"account",S("Account (optional)")}});
    add("github_get_issue_comment","Get a comment",{"owner","repo","comment_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"comment_id",I("Comment ID")},{"account",S("Account (optional)")}});
    add("github_create_issue_comment","Add comment to issue",{"owner","repo","issue_number","body"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"issue_number",I("Issue number")},{"body",S("Comment body")},{"account",S("Account (optional)")}});
    add("github_update_issue_comment","Update a comment",{"owner","repo","comment_id","body"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"comment_id",I("Comment ID")},{"body",S("New body")},{"account",S("Account (optional)")}});
    add("github_delete_issue_comment","Delete a comment",{"owner","repo","comment_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"comment_id",I("Comment ID")},{"account",S("Account (optional)")}});

    // Labels
    add("github_list_labels","List labels",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_get_label","Get a label",{"owner","repo","name"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"name",S("Label name")},{"account",S("Account (optional)")}});
    add("github_create_label","Create a label",{"owner","repo","name","color"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"name",S("Name")},{"color",S("Hex color without #")},{"description",S("Description")},{"account",S("Account (optional)")}});
    add("github_update_label","Update a label",{"owner","repo","name"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"name",S("Current name")},{"new_name",S("New name")},{"color",S("New color")},{"description",S("New description")},{"account",S("Account (optional)")}});
    add("github_delete_label","Delete a label",{"owner","repo","name"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"name",S("Label name")},{"account",S("Account (optional)")}});

    // Milestones
    add("github_list_milestones","List milestones",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"state",S("open/closed/all")},{"account",S("Account (optional)")}});
    add("github_get_milestone","Get a milestone",{"owner","repo","milestone_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"milestone_number",I("Milestone number")},{"account",S("Account (optional)")}});
    add("github_create_milestone","Create a milestone",{"owner","repo","title"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"title",S("Title")},{"description",S("Description")},{"due_on",S("Due date ISO 8601")},{"account",S("Account (optional)")}});
    add("github_update_milestone","Update a milestone",{"owner","repo","milestone_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"milestone_number",I("Milestone number")},{"title",S("Title")},{"description",S("Description")},{"state",S("open/closed")},{"due_on",S("Due date")},{"account",S("Account (optional)")}});
    add("github_delete_milestone","Delete a milestone",{"owner","repo","milestone_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"milestone_number",I("Milestone number")},{"account",S("Account (optional)")}});

    // PRs
    add("github_prs_list","List pull requests",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"state",S("open/closed/all")},{"per_page",I("Per page")},{"page",I("Page")},{"account",S("Account (optional)")}});
    add("github_pr_get","Get a PR",{"owner","repo","pr_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"pr_number",I("PR number")},{"account",S("Account (optional)")}});
    add("github_pr_create","Create a PR",{"owner","repo","title","head","base"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"title",S("Title")},{"head",S("Head branch")},{"base",S("Base branch")},{"body",S("Body")},{"account",S("Account (optional)")}});
    add("github_pr_update","Update a PR",{"owner","repo","pr_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"pr_number",I("PR number")},{"title",S("New title")},{"body",S("New body")},{"state",S("open/closed")},{"account",S("Account (optional)")}});
    add("github_pr_merge","Merge a PR",{"owner","repo","pr_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"pr_number",I("PR number")},{"merge_method",S("merge/squash/rebase")},{"account",S("Account (optional)")}});
    add("github_list_pr_comments","List PR comments",{"owner","repo","pr_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"pr_number",I("PR number")},{"account",S("Account (optional)")}});
    add("github_create_pr_comment","Add comment to PR",{"owner","repo","pr_number","body"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"pr_number",I("PR number")},{"body",S("Comment body")},{"account",S("Account (optional)")}});
    add("github_list_pr_reviews","List PR reviews",{"owner","repo","pr_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"pr_number",I("PR number")},{"account",S("Account (optional)")}});
    add("github_list_pr_files","List files changed in PR",{"owner","repo","pr_number"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"pr_number",I("PR number")},{"account",S("Account (optional)")}});

    // Files
    add("github_file_get","Get a file",{"owner","repo","path"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"path",S("File path")},{"ref",S("Branch/SHA (optional)")},{"account",S("Account (optional)")}});
    add("github_file_create","Create a file",{"owner","repo","path","message","content"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"path",S("File path")},{"message",S("Commit message")},{"content",S("Plain text content")},{"branch",S("Branch (optional)")},{"account",S("Account (optional)")}});
    add("github_file_update","Update a file",{"owner","repo","path","message","content","sha"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"path",S("File path")},{"message",S("Commit message")},{"content",S("New content")},{"sha",S("Current blob SHA")},{"branch",S("Branch (optional)")},{"account",S("Account (optional)")}});
    add("github_file_delete","Delete a file",{"owner","repo","path","message","sha"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"path",S("File path")},{"message",S("Commit message")},{"sha",S("Blob SHA")},{"branch",S("Branch (optional)")},{"account",S("Account (optional)")}});
    add("github_list_directory","List directory contents",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"path",S("Path (empty = root)")},{"ref",S("Branch/SHA (optional)")},{"account",S("Account (optional)")}});

    // Releases
    add("github_list_releases","List releases",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"per_page",I("Per page")},{"account",S("Account (optional)")}});
    add("github_get_release","Get a release by ID",{"owner","repo","release_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"release_id",I("Release ID")},{"account",S("Account (optional)")}});
    add("github_get_latest_release","Get latest release",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_get_release_by_tag","Get release by tag",{"owner","repo","tag"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"tag",S("Tag name")},{"account",S("Account (optional)")}});
    add("github_create_release","Create a release",{"owner","repo","tag_name"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"tag_name",S("Tag")},{"name",S("Release title")},{"body",S("Release notes")},{"draft",B("Draft?")},{"prerelease",B("Pre-release?")},{"target_commitish",S("Branch/SHA for tag")},{"account",S("Account (optional)")}});
    add("github_update_release","Update a release",{"owner","repo","release_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"release_id",I("Release ID")},{"tag_name",S("Tag")},{"name",S("Title")},{"body",S("Notes")},{"draft",B("Draft?")},{"prerelease",B("Pre-release?")},{"account",S("Account (optional)")}});
    add("github_delete_release","Delete a release",{"owner","repo","release_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"release_id",I("Release ID")},{"account",S("Account (optional)")}});

    // Webhooks
    add("github_list_webhooks","List webhooks",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_get_webhook","Get a webhook",{"owner","repo","hook_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"hook_id",I("Hook ID")},{"account",S("Account (optional)")}});
    add("github_create_webhook","Create a webhook",{"owner","repo","url"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"url",S("Payload URL")},{"events",A("Events (e.g. [\"push\",\"pull_request\"])")},{"content_type",S("json/form")},{"active",B("Active?")},{"account",S("Account (optional)")}});
    add("github_update_webhook","Update a webhook",{"owner","repo","hook_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"hook_id",I("Hook ID")},{"url",S("New URL")},{"events",A("Events")},{"active",B("Active?")},{"account",S("Account (optional)")}});
    add("github_delete_webhook","Delete a webhook",{"owner","repo","hook_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"hook_id",I("Hook ID")},{"account",S("Account (optional)")}});
    add("github_ping_webhook","Ping a webhook",{"owner","repo","hook_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"hook_id",I("Hook ID")},{"account",S("Account (optional)")}});

    // Actions
    add("github_list_workflows","List workflows",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"account",S("Account (optional)")}});
    add("github_get_workflow","Get a workflow",{"owner","repo","workflow_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"workflow_id",S("Workflow ID or filename")},{"account",S("Account (optional)")}});
    add("github_list_workflow_runs","List workflow runs",{"owner","repo"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"workflow_id",S("Workflow ID (optional)")},{"status",S("queued/in_progress/completed (optional)")},{"per_page",I("Per page")},{"account",S("Account (optional)")}});
    add("github_get_workflow_run","Get a workflow run",{"owner","repo","run_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"run_id",I("Run ID")},{"account",S("Account (optional)")}});
    add("github_cancel_workflow_run","Cancel a workflow run",{"owner","repo","run_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"run_id",I("Run ID")},{"account",S("Account (optional)")}});
    add("github_rerun_workflow","Re-run a workflow",{"owner","repo","run_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"run_id",I("Run ID")},{"account",S("Account (optional)")}});
    add("github_list_run_artifacts","List artifacts of a run",{"owner","repo","run_id"},
        QJsonObject{{"owner",S("Owner")},{"repo",S("Repo")},{"run_id",I("Run ID")},{"account",S("Account (optional)")}});

    // Gists
    add("github_list_gists","List authenticated user's gists",{},
        QJsonObject{{"per_page",I("Per page")},{"account",S("Account (optional)")}});
    add("github_get_gist","Get a gist",{"gist_id"},
        QJsonObject{{"gist_id",S("Gist ID")},{"account",S("Account (optional)")}});
    add("github_create_gist","Create a gist",{"filename","content"},
        QJsonObject{{"filename",S("Filename")},{"content",S("File content")},{"description",S("Description")},{"public",B("Public?")},{"account",S("Account (optional)")}});
    add("github_update_gist","Update a gist",{"gist_id"},
        QJsonObject{{"gist_id",S("Gist ID")},{"filename",S("Filename to update")},{"content",S("New content")},{"description",S("New description")},{"account",S("Account (optional)")}});
    add("github_delete_gist","Delete a gist",{"gist_id"},
        QJsonObject{{"gist_id",S("Gist ID")},{"account",S("Account (optional)")}});
    add("github_list_gist_comments","List gist comments",{"gist_id"},
        QJsonObject{{"gist_id",S("Gist ID")},{"account",S("Account (optional)")}});
    add("github_create_gist_comment","Add comment to gist",{"gist_id","body"},
        QJsonObject{{"gist_id",S("Gist ID")},{"body",S("Comment body")},{"account",S("Account (optional)")}});
    add("github_delete_gist_comment","Delete gist comment",{"gist_id","comment_id"},
        QJsonObject{{"gist_id",S("Gist ID")},{"comment_id",I("Comment ID")},{"account",S("Account (optional)")}});

    // Search
    add("github_search_code","Search code",{"query"},
        QJsonObject{{"query",S("Search query")},{"per_page",I("Per page")},{"account",S("Account (optional)")}});
    add("github_search_repos","Search repositories",{"query"},
        QJsonObject{{"query",S("Search query")},{"per_page",I("Per page")},{"account",S("Account (optional)")}});
    add("github_search_issues","Search issues and PRs",{"query"},
        QJsonObject{{"query",S("Search query")},{"per_page",I("Per page")},{"account",S("Account (optional)")}});
    add("github_search_users","Search users",{"query"},
        QJsonObject{{"query",S("Search query")},{"per_page",I("Per page")},{"account",S("Account (optional)")}});
    add("github_search_commits","Search commits",{"query"},
        QJsonObject{{"query",S("Search query")},{"per_page",I("Per page")},{"account",S("Account (optional)")}});

    // Rate limit
    add("github_get_rate_limit","Get API rate limit status",{},acct);

    // ── Traffic logging ───────────────────────────────────────────────────────
    add("github_set_logging","Configure HTTP traffic logging",{},
        QJsonObject{{"level",S("Log level: none / summary / headers / full")},
                    {"log_file",S("Path to log file (appended, optional)")},
                    {"max_body_bytes",I("Max bytes of request/response body to log (default 4096)")}});
    add("github_get_logging_status","Get current traffic logging level",{},{});

    // ── Cache ─────────────────────────────────────────────────────────────────
    add("github_set_cache","Configure in-memory response cache",{},
        QJsonObject{{"enabled",B("Enable cache (default false)")},
                    {"ttl_seconds",I("Cache TTL in seconds (default 60)")},
                    {"max_entries",I("Max cached entries, LRU eviction (default 512)")}});
    add("github_clear_cache","Clear all cached responses",{},{});
    add("github_get_cache_stats","Get cache hit/miss statistics",{},{});
    add("github_invalidate_cache","Invalidate cache for a repository",{"owner"},
        QJsonObject{{"owner",S("Repository owner")},
                    {"repo",S("Repository name (optional, invalidates all repos for owner if omitted)")},
                    {"account",S("Account name (optional)")}});

    // ── Pagination ────────────────────────────────────────────────────────────
    add("github_set_pagination","Configure automatic pagination for list operations",{},
        QJsonObject{{"auto_fetch_all",B("If true, automatically fetch all pages and return combined array (default false)")},
                    {"max_pages",I("Maximum pages to fetch when auto_fetch_all=true (default 100, prevents runaway)")}}); 

    // ── Git repository sync ───────────────────────────────────────────────────
    add("github_sync_repository",
        "Synchronize a local git repository with a remote GitHub repository. "
        "Stages all changes, commits, and pushes to the specified branch. "
        "Initializes the repo and configures the authenticated remote automatically "
        "using the token and username from the selected account. "
        "The token is embedded in the remote URL only for the duration of the push "
        "and is removed afterwards.",
        {"local_path","owner","repo"},
        QJsonObject{
            {"local_path",     S("Absolute path to the local git repository")},
            {"owner",          S("Repository owner (user or org)")},
            {"repo",           S("Repository name")},
            {"branch",         S("Target branch on the remote (default: main)")},
            {"remote",         S("Git remote name (default: origin)")},
            {"commit_message", S("Commit message for any uncommitted local changes (default: Sync)")},
            {"force_push",     B("Force-push to remote (default: false). Use with caution.")},
            {"account",        S("Account name (optional, uses first if omitted)")}
        });

    // ── Project upload ────────────────────────────────────────────────────────
    add("github_upload_project",
        "Upload a local project directory to a GitHub repository. "
        "Creates or updates each file via the Contents API. "
        "Binary files and common build artifacts are skipped by default.",
        {"local_path","owner","repo"},
        QJsonObject{
            {"local_path",    S("Absolute path to the local project directory to upload")},
            {"owner",         S("Repository owner (user or org)")},
            {"repo",          S("Repository name")},
            {"branch",        S("Target branch (default: main)")},
            {"commit_message",S("Commit message prefix (default: 'Upload')")},
            {"ignore",        A("Glob patterns to exclude (e.g. [\"*.log\",\"dist\"]). "
                               "Defaults to common build artefacts and .git if omitted.")},
            {"account",       S("Account name (optional, uses first if omitted)")}
        });

    return tools;
}
