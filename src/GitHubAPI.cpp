#include "GitHubAPI.h"
#include "TrafficLogger.h"
#include "ResponseCache.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QRegularExpression>

static const int REQUEST_TIMEOUT_MS = 30000;

GitHubAPI::GitHubAPI(QObject *parent) : QObject(parent) {}

void GitHubAPI::setAccount(const GitHubAccount &account) { m_account = account; }

// ── Request builder ───────────────────────────────────────────────────────────

QNetworkRequest GitHubAPI::buildRequest(const QString &url,
                                         const QString &etag,
                                         const QString &lastModified) const
{
    QUrl qurl(url);
    QNetworkRequest req{qurl};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setRawHeader("User-Agent", "GitHubMCPServer/2.0");

    if (!m_account.token().isEmpty())
        req.setRawHeader("Authorization",
                         QByteArray("Bearer ") + m_account.token().toUtf8());

    // Conditional GET для валидации кэша
    if (!etag.isEmpty())
        req.setRawHeader("If-None-Match", etag.toUtf8());
    else if (!lastModified.isEmpty())
        req.setRawHeader("If-Modified-Since", lastModified.toUtf8());

    return req;
}

// ── Response parser ───────────────────────────────────────────────────────────

ApiResponse GitHubAPI::parseReply(QNetworkReply *reply,
                                   const QByteArray &rawBody,
                                   qint64 elapsedMs) const
{
    ApiResponse resp;
    resp.httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Log response
    gTrafficLogger.logResponse(
        reply->operation() == QNetworkAccessManager::GetOperation ? "GET" :
        reply->operation() == QNetworkAccessManager::PostOperation ? "POST" :
        reply->operation() == QNetworkAccessManager::PutOperation  ? "PUT" : "OTHER",
        reply->url(), resp.httpCode, rawBody, elapsedMs,
        reply->rawHeaderPairs());

    // HTTP 304 Not Modified — данные берём из кэша (тело пустое)
    if (resp.httpCode == 304) {
        resp.success = true;
        return resp;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(rawBody, &err);
    if (err.error == QJsonParseError::NoError) {
        if (doc.isArray()) { resp.isArray = true; resp.array = doc.array(); }
        else               { resp.data = doc.object(); }
    }

    resp.success = (resp.httpCode >= 200 && resp.httpCode < 300);
    if (!resp.success)
        resp.errorMessage = resp.data.value("message").toString(reply->errorString());

    reply->deleteLater();
    return resp;
}

// ── Core send ─────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::sendRequest(const QNetworkRequest &request,
                                    const QByteArray &verb,
                                    const QByteArray &data)
{
    gTrafficLogger.logRequest(verb, request, data);

    QNetworkReply *reply = nullptr;
    if      (verb == "GET")    reply = m_nam.get(request);
    else if (verb == "POST")   reply = m_nam.post(request, data);
    else if (verb == "PUT")    reply = m_nam.put(request, data);
    else if (verb == "PATCH")  { QNetworkRequest r2=request; reply = m_nam.sendCustomRequest(r2,"PATCH",data); }
    else if (verb == "DELETE") {
        if (data.isEmpty()) reply = m_nam.deleteResource(request);
        else { QNetworkRequest r2=request; reply = m_nam.sendCustomRequest(r2,"DELETE",data); }
    }

    if (!reply) {
        ApiResponse r; r.success=false; r.errorMessage="Unknown HTTP verb"; return r;
    }

    QElapsedTimer timer; timer.start();
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(REQUEST_TIMEOUT_MS, &loop, &QEventLoop::quit);
    loop.exec();

    qint64 elapsed = timer.elapsed();
    QByteArray rawBody = reply->readAll();
    return parseReply(reply, rawBody, elapsed);
}

// ── Cache-aware GET ───────────────────────────────────────────────────────────

ApiResponse GitHubAPI::get(const QString &endpoint, const QString &qs)
{
    QString url = baseUrl() + endpoint;
    if (!qs.isEmpty()) url += "?" + qs;

    // Try cache
    if (gResponseCache.isEnabled()) {
        if (const CacheEntry *entry = gResponseCache.find(url)) {
            // Возвращаем закэшированный ответ (ETag-валидация не нужна если не истёк TTL)
            ApiResponse cached;
            cached.success  = true;
            cached.httpCode = 200;
            QJsonParseError e;
            QJsonDocument doc = QJsonDocument::fromJson(entry->body, &e);
            if (e.error == QJsonParseError::NoError) {
                if (doc.isArray()) { cached.isArray=true; cached.array=doc.array(); }
                else               { cached.data = doc.object(); }
            }
            return cached;
        }

        // Expired: проверяем через ETag/If-Modified-Since
        // (entry == nullptr, но можем попробовать conditional GET с последними известными значениями)
    }

    // Normal request
    ApiResponse resp = sendRequest(buildRequest(url), "GET");

    // Store in cache on success (TTL-based, без ETag)
    if (gResponseCache.isEnabled() && resp.success) {
        QByteArray body = resp.isArray
            ? QJsonDocument(resp.array).toJson(QJsonDocument::Compact)
            : QJsonDocument(resp.data).toJson(QJsonDocument::Compact);
        gResponseCache.store(url, body, {}, {}, "application/json");
    }

    return resp;
}

// ── Auto-pagination ───────────────────────────────────────────────────────────

QString GitHubAPI::parseLinkNext(const QByteArray &linkHeader)
{
    // Link: <https://api.github.com/...?page=2>; rel="next", <...>; rel="last"
    if (linkHeader.isEmpty()) return {};
    static QRegularExpression re(R"(<([^>]+)>;\s*rel="next")");
    QRegularExpressionMatch m = re.match(QString::fromUtf8(linkHeader));
    if (m.hasMatch()) return m.captured(1);
    return {};
}

ApiResponse GitHubAPI::fetchAllPages(const QString &endpoint, const QString &baseQuery)
{
    // First page
    QString url = baseUrl() + endpoint;
    if (!baseQuery.isEmpty()) url += "?" + baseQuery;

    QJsonArray collected;
    int pageCount = 0;
    QString nextUrl = url;
    bool truncated = false;

    while (!nextUrl.isEmpty() && pageCount < m_maxPages) {
        // For pages > 1 we use the full URL from Link header directly
        QNetworkRequest req = buildRequest(nextUrl);
        gTrafficLogger.logRequest("GET", req);

        QNetworkReply *reply = m_nam.get(req);
        QElapsedTimer timer; timer.start();
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(REQUEST_TIMEOUT_MS, &loop, &QEventLoop::quit);
        loop.exec();

        qint64 elapsed = timer.elapsed();
        QByteArray rawBody = reply->readAll();
        QByteArray linkHeader = reply->rawHeader("Link");
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        gTrafficLogger.logResponse("GET", reply->url(), httpCode, rawBody, elapsed,
                                   reply->rawHeaderPairs());
        reply->deleteLater();

        if (httpCode < 200 || httpCode >= 300) {
            ApiResponse err;
            err.success = false;
            err.httpCode = httpCode;
            QJsonParseError pe;
            QJsonDocument doc = QJsonDocument::fromJson(rawBody, &pe);
            if (pe.error == QJsonParseError::NoError)
                err.errorMessage = doc.object().value("message").toString();
            return err;
        }

        QJsonParseError pe;
        QJsonDocument doc = QJsonDocument::fromJson(rawBody, &pe);
        if (pe.error == QJsonParseError::NoError) {
            if (doc.isArray()) {
                for (const QJsonValue &v : doc.array())
                    collected.append(v);
            }
        }

        ++pageCount;
        nextUrl = parseLinkNext(linkHeader);

        if (pageCount >= m_maxPages && !nextUrl.isEmpty()) {
            truncated = true;
            break;
        }
    }

    ApiResponse resp;
    resp.success    = true;
    resp.httpCode   = 200;
    resp.isArray    = true;
    resp.array      = collected;
    resp.totalPages = pageCount;
    resp.totalItems = collected.size();
    resp.truncated  = truncated;
    return resp;
}

// ── POST / PUT / PATCH / DELETE ───────────────────────────────────────────────

ApiResponse GitHubAPI::post(const QString &ep, const QJsonObject &body)
{
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return sendRequest(buildRequest(baseUrl()+ep), "POST", data);
}

ApiResponse GitHubAPI::put(const QString &ep, const QJsonObject &body)
{
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return sendRequest(buildRequest(baseUrl()+ep), "PUT", data);
}

ApiResponse GitHubAPI::patch(const QString &ep, const QJsonObject &body)
{
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return sendRequest(buildRequest(baseUrl()+ep), "PATCH", data);
}

ApiResponse GitHubAPI::del(const QString &ep, const QJsonObject &body)
{
    QByteArray data;
    if (!body.isEmpty()) data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return sendRequest(buildRequest(baseUrl()+ep), "DELETE", data);
}

// ── Cache invalidation ────────────────────────────────────────────────────────

void GitHubAPI::invalidateCacheFor(const QString &owner, const QString &repo)
{
    QString prefix = baseUrl() + "/repos/" + owner;
    if (!repo.isEmpty()) prefix += "/" + repo;
    gResponseCache.invalidate(prefix);
}

// ═════════════════════════════════════════════════════════════════════════════
// API methods — all list methods respect m_autoFetchAll
// ═════════════════════════════════════════════════════════════════════════════

#define LIST_EP(ep, qs) \
    (m_autoFetchAll ? fetchAllPages(ep, qs) : get(ep, qs))

// ── User ──────────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::getAuthenticatedUser() { return get("/user"); }

ApiResponse GitHubAPI::getUser(const QString &username)
{ return get(QString("/users/%1").arg(username)); }

ApiResponse GitHubAPI::listUserRepos(const QString &username, const QString &type,
                                      int perPage, int page)
{
    QUrlQuery q;
    q.addQueryItem("type",     type);
    q.addQueryItem("per_page", QString::number(perPage));
    q.addQueryItem("page",     QString::number(page));
    return LIST_EP(QString("/users/%1/repos").arg(username), q.toString(QUrl::FullyEncoded));
}

ApiResponse GitHubAPI::listAuthenticatedUserRepos(const QString &type, int perPage, int page)
{
    QUrlQuery q;
    q.addQueryItem("type",     type);
    q.addQueryItem("per_page", QString::number(perPage));
    q.addQueryItem("page",     QString::number(page));
    return LIST_EP("/user/repos", q.toString(QUrl::FullyEncoded));
}

ApiResponse GitHubAPI::getUserOrgs(const QString &username)
{ return get(QString("/users/%1/orgs").arg(username)); }

// ── Organizations ─────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::getOrg(const QString &org)
{ return get(QString("/orgs/%1").arg(org)); }

ApiResponse GitHubAPI::listOrgRepos(const QString &org, const QString &type,
                                     int perPage, int page)
{
    QUrlQuery q;
    q.addQueryItem("type",     type);
    q.addQueryItem("per_page", QString::number(perPage));
    q.addQueryItem("page",     QString::number(page));
    return LIST_EP(QString("/orgs/%1/repos").arg(org), q.toString(QUrl::FullyEncoded));
}

ApiResponse GitHubAPI::listOrgMembers(const QString &org)
{ return LIST_EP(QString("/orgs/%1/members").arg(org), {}); }

// ── Repositories ──────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::getRepo(const QString &owner, const QString &repo)
{ return get(QString("/repos/%1/%2").arg(owner,repo)); }

ApiResponse GitHubAPI::createRepo(const QString &name, const QString &description, bool isPrivate)
{
    QJsonObject b; b["name"]=name; b["description"]=description; b["private"]=isPrivate;
    return post("/user/repos", b);
}

ApiResponse GitHubAPI::createOrgRepo(const QString &org, const QString &name,
                                      const QString &description, bool isPrivate)
{
    QJsonObject b; b["name"]=name; b["description"]=description; b["private"]=isPrivate;
    return post(QString("/orgs/%1/repos").arg(org), b);
}

ApiResponse GitHubAPI::deleteRepo(const QString &owner, const QString &repo)
{ return del(QString("/repos/%1/%2").arg(owner,repo)); }

ApiResponse GitHubAPI::forkRepo(const QString &owner, const QString &repo, const QString &org)
{
    QJsonObject b; if (!org.isEmpty()) b["organization"]=org;
    return post(QString("/repos/%1/%2/forks").arg(owner,repo), b);
}

ApiResponse GitHubAPI::listForks(const QString &owner, const QString &repo)
{ return LIST_EP(QString("/repos/%1/%2/forks").arg(owner,repo), {}); }

ApiResponse GitHubAPI::listStargazers(const QString &owner, const QString &repo)
{ return LIST_EP(QString("/repos/%1/%2/stargazers").arg(owner,repo), {}); }

ApiResponse GitHubAPI::listWatchers(const QString &owner, const QString &repo)
{ return LIST_EP(QString("/repos/%1/%2/subscribers").arg(owner,repo), {}); }

ApiResponse GitHubAPI::starRepo(const QString &owner, const QString &repo)
{ return put(QString("/user/starred/%1/%2").arg(owner,repo)); }

ApiResponse GitHubAPI::unstarRepo(const QString &owner, const QString &repo)
{ return del(QString("/user/starred/%1/%2").arg(owner,repo)); }

ApiResponse GitHubAPI::listTopics(const QString &owner, const QString &repo)
{ return get(QString("/repos/%1/%2/topics").arg(owner,repo)); }

ApiResponse GitHubAPI::listContributors(const QString &owner, const QString &repo)
{ return LIST_EP(QString("/repos/%1/%2/contributors").arg(owner,repo), {}); }

ApiResponse GitHubAPI::listLanguages(const QString &owner, const QString &repo)
{ return get(QString("/repos/%1/%2/languages").arg(owner,repo)); }

// ── Branches ──────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listBranches(const QString &owner, const QString &repo)
{ return LIST_EP(QString("/repos/%1/%2/branches").arg(owner,repo), {}); }

ApiResponse GitHubAPI::getBranch(const QString &owner, const QString &repo, const QString &branch)
{ return get(QString("/repos/%1/%2/branches/%3").arg(owner,repo,branch)); }

ApiResponse GitHubAPI::createBranch(const QString &owner, const QString &repo,
                                     const QString &branch, const QString &fromSha)
{
    QJsonObject b; b["ref"]=QString("refs/heads/%1").arg(branch); b["sha"]=fromSha;
    return post(QString("/repos/%1/%2/git/refs").arg(owner,repo), b);
}

ApiResponse GitHubAPI::deleteBranch(const QString &owner, const QString &repo, const QString &branch)
{ return del(QString("/repos/%1/%2/git/refs/heads/%3").arg(owner,repo,branch)); }

ApiResponse GitHubAPI::getBranchProtection(const QString &owner, const QString &repo, const QString &branch)
{ return get(QString("/repos/%1/%2/branches/%3/protection").arg(owner,repo,branch)); }

// ── Tags & Commits ────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listTags(const QString &owner, const QString &repo)
{ return LIST_EP(QString("/repos/%1/%2/tags").arg(owner,repo), {}); }

ApiResponse GitHubAPI::listCommits(const QString &owner, const QString &repo,
                                    const QString &sha, const QString &path,
                                    const QString &author, int perPage, int page)
{
    QUrlQuery q;
    if (!sha.isEmpty())    q.addQueryItem("sha",    sha);
    if (!path.isEmpty())   q.addQueryItem("path",   path);
    if (!author.isEmpty()) q.addQueryItem("author", author);
    q.addQueryItem("per_page", QString::number(perPage));
    q.addQueryItem("page",     QString::number(page));
    return LIST_EP(QString("/repos/%1/%2/commits").arg(owner,repo), q.toString(QUrl::FullyEncoded));
}

ApiResponse GitHubAPI::getCommit(const QString &owner, const QString &repo, const QString &sha)
{ return get(QString("/repos/%1/%2/commits/%3").arg(owner,repo,sha)); }

ApiResponse GitHubAPI::compareCommits(const QString &owner, const QString &repo,
                                       const QString &base, const QString &head)
{ return get(QString("/repos/%1/%2/compare/%3...%4").arg(owner,repo,base,head)); }

// ── Issues ────────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listIssues(const QString &owner, const QString &repo,
                                   const QString &state, const QString &labels,
                                   int perPage, int page)
{
    QUrlQuery q;
    if (!state.isEmpty())  q.addQueryItem("state",  state);
    if (!labels.isEmpty()) q.addQueryItem("labels", labels);
    q.addQueryItem("per_page", QString::number(perPage));
    q.addQueryItem("page",     QString::number(page));
    return LIST_EP(QString("/repos/%1/%2/issues").arg(owner,repo), q.toString(QUrl::FullyEncoded));
}

ApiResponse GitHubAPI::getIssue(const QString &owner, const QString &repo, int number)
{ return get(QString("/repos/%1/%2/issues/%3").arg(owner,repo).arg(number)); }

ApiResponse GitHubAPI::createIssue(const QString &owner, const QString &repo,
                                    const QString &title, const QString &body,
                                    const QString &labels, int milestone, const QString &assignees)
{
    QJsonObject obj; obj["title"]=title; obj["body"]=body;
    if (!labels.isEmpty()) {
        QJsonArray arr;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        for (const QString &l : labels.split(',', Qt::SkipEmptyParts)) arr.append(l.trimmed());
#else
        for (const QString &l : labels.split(',', QString::SkipEmptyParts)) arr.append(l.trimmed());
#endif
        obj["labels"]=arr;
    }
    if (milestone>0) obj["milestone"]=milestone;
    if (!assignees.isEmpty()) {
        QJsonArray arr;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        for (const QString &a : assignees.split(',', Qt::SkipEmptyParts)) arr.append(a.trimmed());
#else
        for (const QString &a : assignees.split(',', QString::SkipEmptyParts)) arr.append(a.trimmed());
#endif
        obj["assignees"]=arr;
    }
    return post(QString("/repos/%1/%2/issues").arg(owner,repo), obj);
}

ApiResponse GitHubAPI::updateIssue(const QString &owner, const QString &repo, int number,
                                    const QString &state, const QString &title, const QString &body)
{
    QJsonObject obj;
    if (!state.isEmpty()) obj["state"]=state;
    if (!title.isEmpty()) obj["title"]=title;
    if (!body.isEmpty())  obj["body"] =body;
    return patch(QString("/repos/%1/%2/issues/%3").arg(owner,repo).arg(number), obj);
}

ApiResponse GitHubAPI::closeIssue(const QString &owner, const QString &repo, int number)
{ QJsonObject obj; obj["state"]="closed"; return patch(QString("/repos/%1/%2/issues/%3").arg(owner,repo).arg(number),obj); }

// ── Issue Comments ────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listIssueComments(const QString &owner, const QString &repo, int n)
{ return LIST_EP(QString("/repos/%1/%2/issues/%3/comments").arg(owner,repo).arg(n), {}); }

ApiResponse GitHubAPI::getIssueComment(const QString &owner, const QString &repo, int id)
{ return get(QString("/repos/%1/%2/issues/comments/%3").arg(owner,repo).arg(id)); }

ApiResponse GitHubAPI::createIssueComment(const QString &owner, const QString &repo,
                                           int n, const QString &body)
{ QJsonObject obj; obj["body"]=body; return post(QString("/repos/%1/%2/issues/%3/comments").arg(owner,repo).arg(n),obj); }

ApiResponse GitHubAPI::updateIssueComment(const QString &owner, const QString &repo,
                                           int id, const QString &body)
{ QJsonObject obj; obj["body"]=body; return patch(QString("/repos/%1/%2/issues/comments/%3").arg(owner,repo).arg(id),obj); }

ApiResponse GitHubAPI::deleteIssueComment(const QString &owner, const QString &repo, int id)
{ return del(QString("/repos/%1/%2/issues/comments/%3").arg(owner,repo).arg(id)); }

// ── Labels ────────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listLabels(const QString &owner, const QString &repo)
{ return LIST_EP(QString("/repos/%1/%2/labels").arg(owner,repo), {}); }

ApiResponse GitHubAPI::getLabel(const QString &owner, const QString &repo, const QString &name)
{ return get(QString("/repos/%1/%2/labels/%3").arg(owner,repo,name)); }

ApiResponse GitHubAPI::createLabel(const QString &owner, const QString &repo,
                                    const QString &name, const QString &color, const QString &desc)
{
    QJsonObject obj; obj["name"]=name; obj["color"]=color;
    if (!desc.isEmpty()) obj["description"]=desc;
    return post(QString("/repos/%1/%2/labels").arg(owner,repo), obj);
}

ApiResponse GitHubAPI::updateLabel(const QString &owner, const QString &repo,
                                    const QString &name, const QString &newName,
                                    const QString &color, const QString &desc)
{
    QJsonObject obj;
    if (!newName.isEmpty()) obj["new_name"]=newName;
    if (!color.isEmpty())   obj["color"]=color;
    if (!desc.isEmpty())    obj["description"]=desc;
    return patch(QString("/repos/%1/%2/labels/%3").arg(owner,repo,name), obj);
}

ApiResponse GitHubAPI::deleteLabel(const QString &owner, const QString &repo, const QString &name)
{ return del(QString("/repos/%1/%2/labels/%3").arg(owner,repo,name)); }

// ── Milestones ────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listMilestones(const QString &owner, const QString &repo, const QString &state)
{
    QString qs = state.isEmpty() ? "" : QString("state=%1").arg(state);
    return LIST_EP(QString("/repos/%1/%2/milestones").arg(owner,repo), qs);
}

ApiResponse GitHubAPI::getMilestone(const QString &owner, const QString &repo, int number)
{ return get(QString("/repos/%1/%2/milestones/%3").arg(owner,repo).arg(number)); }

ApiResponse GitHubAPI::createMilestone(const QString &owner, const QString &repo,
                                        const QString &title, const QString &desc, const QString &dueOn)
{
    QJsonObject obj; obj["title"]=title;
    if (!desc.isEmpty())  obj["description"]=desc;
    if (!dueOn.isEmpty()) obj["due_on"]=dueOn;
    return post(QString("/repos/%1/%2/milestones").arg(owner,repo), obj);
}

ApiResponse GitHubAPI::updateMilestone(const QString &owner, const QString &repo, int number,
                                        const QString &title, const QString &desc,
                                        const QString &state, const QString &dueOn)
{
    QJsonObject obj;
    if (!title.isEmpty()) obj["title"]=title;
    if (!desc.isEmpty())  obj["description"]=desc;
    if (!state.isEmpty()) obj["state"]=state;
    if (!dueOn.isEmpty()) obj["due_on"]=dueOn;
    return patch(QString("/repos/%1/%2/milestones/%3").arg(owner,repo).arg(number), obj);
}

ApiResponse GitHubAPI::deleteMilestone(const QString &owner, const QString &repo, int number)
{ return del(QString("/repos/%1/%2/milestones/%3").arg(owner,repo).arg(number)); }

// ── Pull Requests ─────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listPRs(const QString &owner, const QString &repo,
                                const QString &state, int perPage, int page)
{
    QUrlQuery q;
    if (!state.isEmpty()) q.addQueryItem("state",state);
    q.addQueryItem("per_page",QString::number(perPage));
    q.addQueryItem("page",    QString::number(page));
    return LIST_EP(QString("/repos/%1/%2/pulls").arg(owner,repo), q.toString(QUrl::FullyEncoded));
}

ApiResponse GitHubAPI::getPR(const QString &owner, const QString &repo, int n)
{ return get(QString("/repos/%1/%2/pulls/%3").arg(owner,repo).arg(n)); }

ApiResponse GitHubAPI::createPR(const QString &owner, const QString &repo,
                                 const QString &title, const QString &head,
                                 const QString &base, const QString &body)
{
    QJsonObject obj; obj["title"]=title; obj["head"]=head; obj["base"]=base; obj["body"]=body;
    return post(QString("/repos/%1/%2/pulls").arg(owner,repo), obj);
}

ApiResponse GitHubAPI::updatePR(const QString &owner, const QString &repo, int n,
                                 const QString &title, const QString &body, const QString &state)
{
    QJsonObject obj;
    if (!title.isEmpty()) obj["title"]=title;
    if (!body.isEmpty())  obj["body"] =body;
    if (!state.isEmpty()) obj["state"]=state;
    return patch(QString("/repos/%1/%2/pulls/%3").arg(owner,repo).arg(n), obj);
}

ApiResponse GitHubAPI::mergePR(const QString &owner, const QString &repo,
                                int n, const QString &mergeMethod)
{
    QJsonObject obj; obj["merge_method"]=mergeMethod.isEmpty()?"merge":mergeMethod;
    return put(QString("/repos/%1/%2/pulls/%3/merge").arg(owner,repo).arg(n), obj);
}

ApiResponse GitHubAPI::listPRComments(const QString &owner, const QString &repo, int n)
{ return LIST_EP(QString("/repos/%1/%2/pulls/%3/comments").arg(owner,repo).arg(n), {}); }

ApiResponse GitHubAPI::createPRComment(const QString &owner, const QString &repo,
                                        int n, const QString &body)
{ QJsonObject obj; obj["body"]=body; return post(QString("/repos/%1/%2/issues/%3/comments").arg(owner,repo).arg(n),obj); }

ApiResponse GitHubAPI::listPRReviews(const QString &owner, const QString &repo, int n)
{ return LIST_EP(QString("/repos/%1/%2/pulls/%3/reviews").arg(owner,repo).arg(n), {}); }

ApiResponse GitHubAPI::listPRFiles(const QString &owner, const QString &repo, int n)
{ return LIST_EP(QString("/repos/%1/%2/pulls/%3/files").arg(owner,repo).arg(n), {}); }

// ── Files ─────────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::getFile(const QString &owner, const QString &repo,
                                const QString &path, const QString &ref)
{
    QString qs = ref.isEmpty() ? "" : QString("ref=%1").arg(ref);
    return get(QString("/repos/%1/%2/contents/%3").arg(owner,repo,path), qs);
}

ApiResponse GitHubAPI::createFile(const QString &owner, const QString &repo,
                                   const QString &path, const QString &message,
                                   const QString &content, const QString &branch)
{
    QJsonObject obj; obj["message"]=message;
    obj["content"]=QString::fromLatin1(content.toUtf8().toBase64());
    if (!branch.isEmpty()) obj["branch"]=branch;
    return put(QString("/repos/%1/%2/contents/%3").arg(owner,repo,path), obj);
}

ApiResponse GitHubAPI::updateFile(const QString &owner, const QString &repo,
                                   const QString &path, const QString &message,
                                   const QString &content, const QString &sha, const QString &branch)
{
    QJsonObject obj; obj["message"]=message; obj["sha"]=sha;
    obj["content"]=QString::fromLatin1(content.toUtf8().toBase64());
    if (!branch.isEmpty()) obj["branch"]=branch;
    return put(QString("/repos/%1/%2/contents/%3").arg(owner,repo,path), obj);
}

ApiResponse GitHubAPI::deleteFile(const QString &owner, const QString &repo,
                                   const QString &path, const QString &message,
                                   const QString &sha, const QString &branch)
{
    QJsonObject obj; obj["message"]=message; obj["sha"]=sha;
    if (!branch.isEmpty()) obj["branch"]=branch;
    return del(QString("/repos/%1/%2/contents/%3").arg(owner,repo,path), obj);
}

ApiResponse GitHubAPI::listDirectory(const QString &owner, const QString &repo,
                                      const QString &path, const QString &ref)
{
    QString qs = ref.isEmpty() ? "" : QString("ref=%1").arg(ref);
    return get(QString("/repos/%1/%2/contents/%3").arg(owner,repo,path), qs);
}

// ── Releases ──────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listReleases(const QString &owner, const QString &repo, int perPage)
{
    return LIST_EP(QString("/repos/%1/%2/releases").arg(owner,repo),
                   QString("per_page=%1").arg(perPage));
}

ApiResponse GitHubAPI::getRelease(const QString &owner, const QString &repo, int id)
{ return get(QString("/repos/%1/%2/releases/%3").arg(owner,repo).arg(id)); }

ApiResponse GitHubAPI::getLatestRelease(const QString &owner, const QString &repo)
{ return get(QString("/repos/%1/%2/releases/latest").arg(owner,repo)); }

ApiResponse GitHubAPI::getReleaseByTag(const QString &owner, const QString &repo, const QString &tag)
{ return get(QString("/repos/%1/%2/releases/tags/%3").arg(owner,repo,tag)); }

ApiResponse GitHubAPI::createRelease(const QString &owner, const QString &repo,
                                      const QString &tagName, const QString &name,
                                      const QString &body, bool draft, bool prerelease,
                                      const QString &targetCommitish)
{
    QJsonObject obj; obj["tag_name"]=tagName; obj["name"]=name; obj["body"]=body;
    obj["draft"]=draft; obj["prerelease"]=prerelease;
    if (!targetCommitish.isEmpty()) obj["target_commitish"]=targetCommitish;
    return post(QString("/repos/%1/%2/releases").arg(owner,repo), obj);
}

ApiResponse GitHubAPI::updateRelease(const QString &owner, const QString &repo, int id,
                                      const QString &tagName, const QString &name,
                                      const QString &body, bool draft, bool prerelease)
{
    QJsonObject obj;
    if (!tagName.isEmpty()) obj["tag_name"]=tagName;
    if (!name.isEmpty())    obj["name"]=name;
    if (!body.isEmpty())    obj["body"]=body;
    obj["draft"]=draft; obj["prerelease"]=prerelease;
    return patch(QString("/repos/%1/%2/releases/%3").arg(owner,repo).arg(id), obj);
}

ApiResponse GitHubAPI::deleteRelease(const QString &owner, const QString &repo, int id)
{ return del(QString("/repos/%1/%2/releases/%3").arg(owner,repo).arg(id)); }

// ── Webhooks ──────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listWebhooks(const QString &owner, const QString &repo)
{ return LIST_EP(QString("/repos/%1/%2/hooks").arg(owner,repo), {}); }

ApiResponse GitHubAPI::getWebhook(const QString &owner, const QString &repo, int id)
{ return get(QString("/repos/%1/%2/hooks/%3").arg(owner,repo).arg(id)); }

ApiResponse GitHubAPI::createWebhook(const QString &owner, const QString &repo,
                                      const QString &url, const QStringList &events,
                                      const QString &contentType, bool active)
{
    QJsonObject config; config["url"]=url;
    config["content_type"]=contentType.isEmpty()?"json":contentType;
    QJsonObject obj; obj["name"]="web"; obj["config"]=config; obj["active"]=active;
    QJsonArray evArr;
    for (const QString &e : events) evArr.append(e);
    if (evArr.isEmpty()) evArr.append("push");
    obj["events"]=evArr;
    return post(QString("/repos/%1/%2/hooks").arg(owner,repo), obj);
}

ApiResponse GitHubAPI::updateWebhook(const QString &owner, const QString &repo, int id,
                                      const QString &url, const QStringList &events, bool active)
{
    QJsonObject obj; obj["active"]=active;
    if (!url.isEmpty()) { QJsonObject cfg; cfg["url"]=url; obj["config"]=cfg; }
    if (!events.isEmpty()) {
        QJsonArray arr; for (const QString &e : events) arr.append(e); obj["events"]=arr;
    }
    return patch(QString("/repos/%1/%2/hooks/%3").arg(owner,repo).arg(id), obj);
}

ApiResponse GitHubAPI::deleteWebhook(const QString &owner, const QString &repo, int id)
{ return del(QString("/repos/%1/%2/hooks/%3").arg(owner,repo).arg(id)); }

ApiResponse GitHubAPI::pingWebhook(const QString &owner, const QString &repo, int id)
{ return post(QString("/repos/%1/%2/hooks/%3/pings").arg(owner,repo).arg(id), {}); }

// ── Actions ───────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listWorkflows(const QString &owner, const QString &repo)
{ return get(QString("/repos/%1/%2/actions/workflows").arg(owner,repo)); }

ApiResponse GitHubAPI::getWorkflow(const QString &owner, const QString &repo, const QString &wfId)
{ return get(QString("/repos/%1/%2/actions/workflows/%3").arg(owner,repo,wfId)); }

ApiResponse GitHubAPI::listWorkflowRuns(const QString &owner, const QString &repo,
                                         const QString &wfId, const QString &status, int perPage)
{
    QUrlQuery q;
    if (!status.isEmpty()) q.addQueryItem("status",status);
    q.addQueryItem("per_page",QString::number(perPage));
    if (wfId.isEmpty())
        return LIST_EP(QString("/repos/%1/%2/actions/runs").arg(owner,repo), q.toString(QUrl::FullyEncoded));
    else
        return LIST_EP(QString("/repos/%1/%2/actions/workflows/%3/runs").arg(owner,repo,wfId),
                       q.toString(QUrl::FullyEncoded));
}

ApiResponse GitHubAPI::getWorkflowRun(const QString &owner, const QString &repo, int id)
{ return get(QString("/repos/%1/%2/actions/runs/%3").arg(owner,repo).arg(id)); }

ApiResponse GitHubAPI::cancelWorkflowRun(const QString &owner, const QString &repo, int id)
{ return post(QString("/repos/%1/%2/actions/runs/%3/cancel").arg(owner,repo).arg(id), {}); }

ApiResponse GitHubAPI::rerunWorkflow(const QString &owner, const QString &repo, int id)
{ return post(QString("/repos/%1/%2/actions/runs/%3/rerun").arg(owner,repo).arg(id), {}); }

ApiResponse GitHubAPI::listRunArtifacts(const QString &owner, const QString &repo, int id)
{ return get(QString("/repos/%1/%2/actions/runs/%3/artifacts").arg(owner,repo).arg(id)); }

// ── Gists ─────────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::listGists(int perPage)
{ return LIST_EP("/gists", QString("per_page=%1").arg(perPage)); }

ApiResponse GitHubAPI::getGist(const QString &id)
{ return get(QString("/gists/%1").arg(id)); }

ApiResponse GitHubAPI::createGist(const QString &description, bool isPublic,
                                   const QString &filename, const QString &content)
{
    QJsonObject fc; fc["content"]=content;
    QJsonObject files; files[filename]=fc;
    QJsonObject obj; obj["description"]=description; obj["public"]=isPublic; obj["files"]=files;
    return post("/gists", obj);
}

ApiResponse GitHubAPI::updateGist(const QString &id, const QString &description,
                                   const QString &filename, const QString &content)
{
    QJsonObject obj;
    if (!description.isEmpty()) obj["description"]=description;
    if (!filename.isEmpty() && !content.isEmpty()) {
        QJsonObject fc; fc["content"]=content;
        QJsonObject files; files[filename]=fc;
        obj["files"]=files;
    }
    return patch(QString("/gists/%1").arg(id), obj);
}

ApiResponse GitHubAPI::deleteGist(const QString &id)
{ return del(QString("/gists/%1").arg(id)); }

ApiResponse GitHubAPI::listGistComments(const QString &id)
{ return LIST_EP(QString("/gists/%1/comments").arg(id), {}); }

ApiResponse GitHubAPI::createGistComment(const QString &id, const QString &body)
{ QJsonObject obj; obj["body"]=body; return post(QString("/gists/%1/comments").arg(id), obj); }

ApiResponse GitHubAPI::deleteGistComment(const QString &id, int commentId)
{ return del(QString("/gists/%1/comments/%2").arg(id).arg(commentId)); }

// ── Search ────────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::searchCode(const QString &q, int pp)
{ QUrlQuery qp; qp.addQueryItem("q",q); qp.addQueryItem("per_page",QString::number(pp));
  return get("/search/code", qp.toString(QUrl::FullyEncoded)); }

ApiResponse GitHubAPI::searchRepos(const QString &q, int pp)
{ QUrlQuery qp; qp.addQueryItem("q",q); qp.addQueryItem("per_page",QString::number(pp));
  return get("/search/repositories", qp.toString(QUrl::FullyEncoded)); }

ApiResponse GitHubAPI::searchIssues(const QString &q, int pp)
{ QUrlQuery qp; qp.addQueryItem("q",q); qp.addQueryItem("per_page",QString::number(pp));
  return get("/search/issues", qp.toString(QUrl::FullyEncoded)); }

ApiResponse GitHubAPI::searchUsers(const QString &q, int pp)
{ QUrlQuery qp; qp.addQueryItem("q",q); qp.addQueryItem("per_page",QString::number(pp));
  return get("/search/users", qp.toString(QUrl::FullyEncoded)); }

ApiResponse GitHubAPI::searchCommits(const QString &q, int pp)
{ QUrlQuery qp; qp.addQueryItem("q",q); qp.addQueryItem("per_page",QString::number(pp));
  return get("/search/commits", qp.toString(QUrl::FullyEncoded)); }

// ── Git repository sync ───────────────────────────────────────────────────────

static QString runGit(const QString &workDir,
                      const QStringList &args,
                      const QProcessEnvironment &env,
                      bool *ok = nullptr)
{
    QProcess proc;
    proc.setWorkingDirectory(workDir);
    proc.setProcessEnvironment(env);
    proc.start("git", args);
    proc.waitForFinished(60000);
    QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    QString combined = out;
    if (!err.isEmpty()) combined += (combined.isEmpty() ? "" : "\n") + err;
    if (ok) *ok = (proc.exitCode() == 0 && proc.exitStatus() == QProcess::NormalExit);
    return combined;
}

GitHubAPI::SyncResult GitHubAPI::syncRepository(const QString &localRepoPath,
                                                  const QString &owner,
                                                  const QString &repo,
                                                  const QString &branch,
                                                  const QString &remote,
                                                  const QString &commitMsg,
                                                  bool           forcePush)
{
    SyncResult result;

    QDir repoDir(localRepoPath);
    if (!repoDir.exists()) {
        result.success      = false;
        result.errorMessage = QString("Local path does not exist: %1").arg(localRepoPath);
        return result;
    }

    QString token    = m_account.token();
    QString username = m_account.username();
    if (token.isEmpty()) {
        result.success      = false;
        result.errorMessage = "Account token is empty";
        return result;
    }

    // Build authenticated remote URL
    // https://username:token@github.com/owner/repo.git
    // or for GHE: https://username:token@host/owner/repo.git
    QString baseUrl = m_account.apiBaseUrl();
    // Extract host from apiBaseUrl (strip protocol and /api/v3 suffix)
    QUrl parsedBase(baseUrl);
    QString host = parsedBase.host();
    if (host.isEmpty()) host = "github.com";
    QString remoteUrl = QString("https://%1:%2@%3/%4/%5.git")
                            .arg(username, token, host, owner, repo);

    // Prepare env: pass GIT_TERMINAL_PROMPT=0 so git never hangs waiting for input
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("GIT_TERMINAL_PROMPT", "0");
    env.insert("GIT_ASKPASS", "echo");
    // Suppress credential helper to avoid OS keychain interaction
    env.insert("GCM_INTERACTIVE", "never");

    auto step = [&](const QString &name, const QStringList &args, bool required = true) -> bool {
        bool ok = false;
        QString out = runGit(localRepoPath, args, env, &ok);
        SyncResult::Step s; s.name = name; s.ok = ok; s.output = out;
        result.steps.append(s);
        result.output += QString("[%1] %2\n%3\n").arg(ok?"OK":"FAIL", name, out);
        if (!ok && required) {
            result.errorMessage = QString("Step '%1' failed: %2").arg(name, out);
        }
        return ok;
    };

    // 1. Check this is a git repo (or init)
    bool isRepo = false;
    runGit(localRepoPath, {"rev-parse", "--git-dir"}, env, &isRepo);
    if (!isRepo) {
        if (!step("git init", {"init"})) { return result; }
    }

    // 2. Set/update remote URL with embedded credentials
    bool remoteExists = false;
    runGit(localRepoPath, {"remote", "get-url", remote}, env, &remoteExists);

    if (remoteExists) {
        step("set remote url", {"remote", "set-url", remote, remoteUrl});
    } else {
        if (!step("add remote", {"remote", "add", remote, remoteUrl})) { return result; }
    }

    // 3. Configure user identity if not set (required for commit)
    bool hasName = false;
    runGit(localRepoPath, {"config", "user.name"}, env, &hasName);
    if (!hasName) {
        QString displayName = username.isEmpty() ? "GitHubMCPServer" : username;
        step("config user.name",  {"config", "user.name",  displayName}, false);
        step("config user.email", {"config", "user.email",
             QString("%1@users.noreply.github.com").arg(username)}, false);
    }

    // 4. Stage all changes
    if (!step("git add -A", {"add", "-A"})) { return result; }

    // 5. Commit (may have nothing to commit — that's fine)
    bool commitOk = false;
    QString commitOut = runGit(localRepoPath, {"commit", "-m", commitMsg}, env, &commitOk);
    {
        SyncResult::Step s;
        s.name   = "git commit";
        s.ok     = commitOk || commitOut.contains("nothing to commit");
        s.output = commitOut;
        result.steps.append(s);
        result.output += QString("[%1] git commit\n%2\n")
                             .arg(s.ok ? "OK" : "WARN", commitOut);
    }

    // 6. Push
    QStringList pushArgs = {"push", remote, QString("HEAD:refs/heads/%1").arg(branch)};
    if (forcePush) pushArgs.insert(2, "--force");
    if (!step("git push", pushArgs)) { return result; }

    // 7. Scrub token from remote URL after push for safety
    //    Replace with non-authenticated URL so token doesn't sit in .git/config
    QString safeUrl = QString("https://github.com/%1/%2.git").arg(owner, repo);
    if (m_account.isEnterprise())
        safeUrl = QString("https://%1/%2/%3.git").arg(host, owner, repo);
    step("sanitize remote url", {"remote", "set-url", remote, safeUrl}, false);

    result.success = true;
    return result;
}

// ── Project upload ────────────────────────────────────────────────────────────

static bool matchesGlob(const QString &relPath, const QStringList &patterns)
{
    for (const QString &pat : patterns) {
        QRegularExpression re(QRegularExpression::wildcardToRegularExpression(pat));
        if (re.match(relPath).hasMatch()) return true;
        // also match directory component
        if (relPath.contains('/')) {
            QString dir = relPath.section('/', 0, 0);
            if (QRegularExpression(QRegularExpression::wildcardToRegularExpression(pat)).match(dir).hasMatch())
                return true;
        }
    }
    return false;
}

GitHubAPI::UploadResult GitHubAPI::uploadProject(const QString &localPath,
                                                  const QString &owner,
                                                  const QString &repo,
                                                  const QString &branch,
                                                  const QString &commitPrefix,
                                                  const QStringList &ignore)
{
    UploadResult result;

    QDir rootDir(localPath);
    if (!rootDir.exists()) {
        result.success      = false;
        result.errorMessage = QString("Local path does not exist: %1").arg(localPath);
        return result;
    }

    // Default ignore patterns
    QStringList ignorePatterns = ignore;
    if (ignorePatterns.isEmpty()) {
        ignorePatterns = QStringList{
            ".git", ".git/*",
            "*.o", "*.obj", "*.a", "*.lib", "*.so", "*.dll", "*.dylib", "*.exe",
            "build", "build/*", "CMakeFiles", "CMakeFiles/*",
            "*.user", ".DS_Store", "Thumbs.db"
        };
    }

    // Collect all files
    QStringList filePaths;
    QDirIterator it(localPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QString relPath = rootDir.relativeFilePath(it.filePath());
        if (!matchesGlob(relPath, ignorePatterns))
            filePaths.append(relPath);
    }

    if (filePaths.isEmpty()) {
        result.success = true;
        result.errorMessage = "No files to upload (directory is empty or all files ignored)";
        return result;
    }

    for (const QString &relPath : filePaths) {
        QString absPath = rootDir.absoluteFilePath(relPath);
        QFile f(absPath);
        if (!f.open(QIODevice::ReadOnly)) {
            QJsonObject entry;
            entry["file"]   = relPath;
            entry["status"] = "failed";
            entry["error"]  = "Cannot open file";
            result.log.append(entry);
            ++result.failed;
            continue;
        }
        QByteArray raw = f.readAll();
        f.close();

        // createFile/updateFile accept plain content (they do base64 internally)
        QString content = QString::fromUtf8(raw);

        // Check if file already exists to get its SHA for update
        ApiResponse existing = getFile(owner, repo, relPath, branch);
        QString existingSha;
        if (existing.success && !existing.data.isEmpty())
            existingSha = existing.data.value("sha").toString();

        ApiResponse resp;
        QString msg = QString("%1: %2").arg(commitPrefix, relPath);

        if (existingSha.isEmpty()) {
            resp = createFile(owner, repo, relPath, msg, content, branch);
        } else {
            resp = updateFile(owner, repo, relPath, msg, content, existingSha, branch);
        }

        QJsonObject entry;
        entry["file"] = relPath;
        if (resp.success) {
            entry["status"] = existingSha.isEmpty() ? "created" : "updated";
            ++result.uploaded;
        } else {
            entry["status"] = "failed";
            entry["error"]  = resp.errorMessage;
            ++result.failed;
        }
        result.log.append(entry);
    }

    result.success = (result.failed == 0);
    return result;
}

// ── Rate limit ────────────────────────────────────────────────────────────────

ApiResponse GitHubAPI::getRateLimit() { return get("/rate_limit"); }
