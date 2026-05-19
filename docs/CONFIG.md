# Конфигурация GitHub MCP Server

## Обзор

Конфигурационный файл `accounts.json` содержит список аккаунтов GitHub для работы с API. Поддерживаются несколько аккаунтов одновременно.

## Структура файла

### Базовая структура

```json
{
  "accounts": [
    {
      "name": "account_name",
      "type": "github",
      "baseUrl": "https://api.github.com",
      "token": "your_personal_access_token"
    }
  ]
}
```

### Поле описания

| Поле | Тип | Обязательно | Описание |
|------|-----|-------------|----------|
| `name` | string | ✅ | Уникальное имя аккаунта |
| `type` | string | ✅ | Тип: `github` или `github_enterprise` |
| `baseUrl` | string | ✅ | Базовый URL API |
| `token` | string | ✅ | Персональный токен доступа |

## Примеры конфигураций

### Пример 1: Один аккаунт GitHub

```json
{
  "accounts": [
    {
      "name": "personal-github",
      "type": "github",
      "baseUrl": "https://api.github.com",
      "token": "ghp_16C7e42F292c6912E7710c838347Ae178B4a"
    }
  ]
}
```

### Пример 2: Несколько аккаунтов

```json
{
  "accounts": [
    {
      "name": "personal",
      "type": "github",
      "baseUrl": "https://api.github.com",
      "token": "ghp_PERSONAL_TOKEN"
    },
    {
      "name": "work",
      "type": "github",
      "baseUrl": "https://api.github.com",
      "token": "ghp_WORK_TOKEN"
    },
    {
      "name": "company-enterprise",
      "type": "github_enterprise",
      "baseUrl": "https://github.company.com/api/v3",
      "token": "company_enterprise_token"
    }
  ]
}
```

### Пример 3: С GitHub Enterprise Server

```json
{
  "accounts": [
    {
      "name": "ghes-server",
      "type": "github_enterprise",
      "baseUrl": "https://github.mycompany.com/api/v3",
      "token": "your_enterprise_token"
    }
  ]
}
```

## Получение токена

### Для GitHub.com

1. Перейдите в настройки: **Settings** → **Developer settings** → **Personal access tokens**
2. Выберите вкладку **Tokens (classic)**
3. Нажмите **Generate new token** → **Classic**
4. Заполните форму:

| Поле | Значение |
|------|----------|
| Note | MCP Server |
| Expiration | Например, 30 дней |
| Scopes | Выберите необходимые |

### Необходимые права (Scopes)

| Scope | Описание | Необходим для |
|-------|----------|----------------|
| `repo` | Пол access to repositories | Работа с репозиториями |
| `read:org` | Read orgs, teams, members | Организации |
| `user` | Access user data | Пользователь |
| `workflow` | Update repo workflows | CI/CD workflows |
| `write:packages` | Manage packages | Packages |

### Рекомендуемые права

```
repo
read:org
user
workflow
write:packages
```

## Безопасность

### Шифрование токена

При сохранении токен **автоматически шифруется**:
- Используется AES-256
- Ключ хранится в защищённом хранилище
- Токен не сохраняется в открытом виде

### Права файла

```bash
# Установите правильные права
chmod 600 accounts.json

# Проверьте права
ls -la accounts.json
# -rw------- 1 user user 256 May 19 12:00 accounts.json
```

### Игнорирование в Git

Добавьте в `.gitignore`:
```
accounts.json
*.key
```

## Загрузка конфигурации

### По умолчанию

```bash
./build/github_mcp_server
# Ищет accounts.json в текущей директории
```

### С указанием пути

```bash
./build/github_mcp_server --config /path/to/accounts.json
```

### Проверка конфигурации

```bash
# Валидация JSON
python3 -m json.tool accounts.json

# Или через jq
jq . accounts.json
```

## Переменные окружения

| Переменная | Описание | Пример |
|------------|----------|--------|
| `GITHUB_TOKEN` | Токен по умолчанию | `ghp_xxx` |
| `GITHUB_CONFIG` | Путь к конфигу | `/etc/mcp/accounts.json` |

## Миграция конфигурации

### Из старого формата

Старый формат:
```json
{
  "github_token": "ghp_xxx"
}
```

Новый формат:
```json
{
  "accounts": [
    {
      "name": "default",
      "type": "github",
      "baseUrl": "https://api.github.com",
      "token": "ghp_xxx"
    }
  ]
}
```

## Устранение неполадок

### Ошибка: "Invalid token"

1. Проверьте токен в консоли:
```bash
curl -H "Authorization: token ghp_YOUR_TOKEN" https://api.github.com/user
```

2. Проверьте права токена

3. Проверьте срок действия

### Ошибка: "Rate limit exceeded"

```bash
# Проверьте лимит
curl -H "Authorization: token ghp_YOUR_TOKEN" https://api.github.com/rate_limit
```

### "Account not found"

Убедитесь, что имя аккаунта указано правильно:
```json
{
  "method": "github_get_repo",
  "params": {
    "account": "personal-github",  // Должно совпадать с name в accounts.json
    ...
  }
}
```

## Шаблоны конфигураций

### Минимальная конфигурация

```json
{
  "accounts": [
    {
      "name": "default",
      "type": "github",
      "baseUrl": "https://api.github.com",
      "token": "ghp_YOUR_TOKEN"
    }
  ]
}
```

### Продакшен конфигурация

```json
{
  "accounts": [
    {
      "name": "prod",
      "type": "github",
      "baseUrl": "https://api.github.com",
      "token": "${GITHUB_TOKEN_PROD}"
    },
    {
      "name": "staging",
      "type": "github",
      "baseUrl": "https://api.github.com",
      "token": "${GITHUB_TOKEN_STAGING}"
    }
  ]
}
```

---

**Версия**: 1.0  
**Последнее обновление**: 2026-05-19