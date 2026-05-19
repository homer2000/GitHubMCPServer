# Справочник API GitHub MCP Server

## Обзор

GitHub MCP Server предоставляет 100+ инструментов для работы с GitHub API через протокол JSON-RPC 2.0.

## Формат запроса

### Базовая структура

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "название_метода",
  "params": {
    "account": "имя_аккаунта",
    "параметры_метода": "значение"
  }
}
```

### Ответ успеха

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "data": {...},
    "status": "success"
  }
}
```

### Ответ с ошибкой

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32000,
    "message": "Error description"
  }
}
```

## Категории инструментов

### 1. Пользователь (User)

#### github_get_authenticated_user
Получить текущего пользователя.

**Параметры**:
```json
{
  "account": "account_name"
}
```

**Ответ**:
```json
{
  "login": "username",
  "id": 12345,
  "avatar_url": "...",
  "type": "User"
}
```

#### github_get_user
Получить информацию о пользователе.

**Параметры**:
```json
{
  "account": "account_name",
  "username": "octocat"
}
```

---

### 2. Репозитории (Repositories)

#### github_get_repo
Получить информацию о репозитории.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World"
}
```

#### github_list_repos
Получить список репозиториев пользователя.

**Параметры**:
```json
{
  "account": "account_name",
  "username": "octocat",
  "type": "owner"
}
```

#### github_create_repo
Создать новый репозиторий.

**Параметры**:
```json
{
  "account": "account_name",
  "name": "new-repo",
  "description": "Repo description",
  "private": true
}
```

---

### 3. Issues (Задачи)

#### github_create_issue
Создать новой issue.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "title": "Bug report",
  "body": "Description",
  "labels": ["bug", "help wanted"]
}
```

#### github_list_issues
Получить список issue.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "state": "open"
}
```

#### github_get_issue
Получить конкретный issue.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "issue_number": 1347
}
```

#### github_update_issue
Обновить issue.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "issue_number": 1347,
  "title": "Updated title",
  "state": "closed"
}
```

---

### 4. Pull Requests

#### github_create_pr
Создать pull request.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "title": "PR title",
  "head": "feature-branch",
  "base": "main",
  "body": "Description"
}
```

#### github_list_prs
Получить список pull requests.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "state": "open"
}
```

#### github_get_pr
Получить конкретный pull request.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "pr_number": 1
}
```

#### github_merge_pr
Слить pull request.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "pr_number": 1,
  "merge_method": "merge"
}
```

---

### 5. Ветки (Branches)

#### github_create_branch
Создать ветку.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "branch": "feature/new-feature",
  "from_branch": "main"
}
```

#### github_get_branch
Получить информацию о ветке.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "branch": "main"
}
```

#### github_list_branches
Получить список веток.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World"
}
```

---

### 6. Коммиты (Commits)

#### github_list_commits
Получить историю коммитов.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "branch": "main"
}
```

#### github_get_commit
Получить конкретный коммит.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "commit_sha": "6dcb09b5b578a998c76e7a579f0c85a47d0b9c0d"
}
```

---

### 7. Релизы (Releases)

#### github_create_release
Создать релиз.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "tag_name": "v1.0.0",
  "name": "Version 1.0.0",
  "body": "Release notes",
  "draft": false
}
```

#### github_list_releases
Получить список релизов.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World"
}
```

#### github_get_latest_release
Получить последний релиз.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World"
}
```

---

### 8. Вебхуки (Webhooks)

#### github_create_webhook
Создать вебхук.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World",
  "name": "web",
  "config": {
    "url": "https://example.com/webhook",
    "content_type": "json"
  },
  "events": ["push", "pull_request"]
}
```

#### github_list_webhooks
Получить список вебхуков.

**Параметры**:
```json
{
  "account": "account_name",
  "owner": "octocat",
  "repo": "Hello-World"
}
```

---

### 9. Организации (Organizations)

#### github_get_org
Получить информацию об организации.

**Параметры**:
```json
{
  "account": "account_name",
  "org": "github"
}
```

#### github_list_org_members
Получить членов организации.

**Параметры**:
```json
{
  "account": "account_name",
  "org": "github"
}
```

#### github_list_org_repos
Получить репозитории организации.

**Параметры**:
```json
{
  "account": "account_name",
  "org": "github"
}
```

---

### 10. Поиск (Search)

#### github_search_repos
Поиск репозиториев.

**Параметры**:
```json
{
  "account": "account_name",
  "query": "topic:topic-name",
  "per_page": 10
}
```

#### github_search_users
Поиск пользователей.

**Параметры**:
```json
{
  "account": "account_name",
  "query": "username",
  "per_page": 10
}
```

#### github_search_issues
Поиск issue.

**Параметры**:
```json
{
  "account": "account_name",
  "query": "repo:owner/repo is:issue is:open",
  "per_page": 10
}
```

---

## Полный список инструментов

| № | Инструмент | Категория |
|---|------------|-----------|
| 1 | github_get_authenticated_user | User |
| 2 | github_get_user | User |
| 3 | github_get_repo | Repositories |
| 4 | github_list_repos | Repositories |
| 5 | github_create_repo | Repositories |
| 6 | github_get_branch | Branches |
| 7 | github_create_branch | Branches |
| 8 | github_list_branches | Branches |
| 9 | github_get_commit | Commits |
| 10 | github_list_commits | Commits |
| 11 | github_create_issue | Issues |
| 12 | github_list_issues | Issues |
| 13 | github_get_issue | Issues |
| 14 | github_update_issue | Issues |
| 15 | github_delete_issue | Issues |
| 16 | github_create_pr | Pull Requests |
| 17 | github_list_prs | Pull Requests |
| 18 | github_get_pr | Pull Requests |
| 19 | github_merge_pr | Pull Requests |
| 20 | github_create_label | Labels |
| 21 | github_list_labels | Labels |
| 22 | github_create_milestone | Milestones |
| 23 | github_list_milestones | Milestones |
| 24 | github_create_release | Releases |
| 25 | github_list_releases | Releases |
| 26 | github_get_latest_release | Releases |
| 27 | github_create_webhook | Webhooks |
| 28 | github_list_webhooks | Webhooks |
| 29 | github_get_contents | Contents |
| 30 | github_create_file | Contents |
| 31 | github_update_file | Contents |
| 32 | github_delete_file | Contents |
| 33 | github_search_repos | Search |
| 34 | github_search_users | Search |
| 35 | github_search_issues | Search |
| 36 | github_get_org | Organizations |
| 37 | github_list_org_members | Organizations |
| 38 | github_list_org_repos | Organizations |
| 39 | github_create_gist | Gists |
| 40 | github_list_gists | Gists |
| 41 | github_get_gist | Gists |
| 42 | github_run_workflow | Workflows |
| 43 | github_list_workflow_runs | Workflows |
| 44 | github_get_workflow | Workflows |
| 45 | github_create_deployment | Deployments |
| 46 | github_list_deployments | Deployments |
| 47 | github_get_deployment | Deployments |
| 48 | github_download_release_asset | Releases |
| 49 | github_create_project | Projects |
| 50 | github_list_projects | Projects |
| ... | ... | ... |
| **100+** | ... | ... |

---

## Примеры использования

### Создание issue через MCP

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"github_create_issue","params":{"account":"personal","owner":"myorg","repo":"myrepo","title":"Bug report","body":"Description"}}' | ./build/github_mcp_server
```

### Получение списка репозиториев

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"github_list_repos","params":{"account":"personal"}}' | ./build/github_mcp_server
```

### Обновление issue

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"github_update_issue","params":{"account":"personal","owner":"org","repo":"repo","issue_number":1,"state":"closed"}}' | ./build/github_mcp_server
```

---

**Версия**: 1.0  
**Последнее обновление**: 2026-05-19