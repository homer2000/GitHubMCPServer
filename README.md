# GitHub MCP Server

Многофункциональный MCP (Model Context Protocol) сервер для работы с GitHub API. Реализован на C++/Qt с поддержкой 100+ инструментов.

## Оглавление

| Документ | Описание |
|----------|----------|
| [Архитектура](docs/ARCHITECTURE.md) | Структура проекта и компоненты |
| [Сборка](docs/BUILD.md) | Как собрать проект с разными Qt версиями |
| [Конфигурация](docs/CONFIG.md) | Настройка аккаунтов и токенов |
| [API Справочник](docs/API.md) | Список всех 100+ инструментов |
| [Устранение неполадок](docs/TROUBLESHOOTING.md) | Решение проблем при сборке и запуске |

## Быстрый старт

### 1. Сборка

```bash
# Автоматическая сборка через MCP инструменты
mcp__qt-build__build_qt6_10_2_gcc_64 --project_dir $(pwd)

# Или вручную:
mkdir build && cd build
qmake ../github_mcp_server.pro
make -j$(nproc)
```

### 2. Конфигурация

```bash
cp accounts.json.example accounts.json
# Отредактируйте accounts.json, добавив ваши токены
```

### 3. Запуск

```bash
./build/github_mcp_server --config accounts.json
```

## Результаты сборки

| Qt Версия | Статус | Предупреждения | Размер |
|-----------|--------|----------------|--------|
| Qt 5.15.2 | ✅ | 2 (C++17 ext) | 840KB |
| Qt 6.5.1 | ✅ | 0 | 789KB |
| Qt 6.7.2 | ✅ | 0 | ~790KB |
| Qt 6.10.2 | ✅ | 0 | ~790KB |

**Рекомендуемая версия**: Qt 6.10.2

## Особенности

- ✅ 100+ MCP инструментов для GitHub API
- ✅ Поддержка GitHub.com и GitHub Enterprise
- ✅ JSON-RPC 2.0 над stdin/stdout
- ✅ LRU-кэш с TTL
- ✅ Логирование HTTP трафика
- ✅ Множественные аккаунты

## Примеры использования

### Создание issue

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "github_create_issue",
  "params": {
    "account": "my-github",
    "owner": "octocat",
    "repo": "Hello-World",
    "title": "Bug report",
    "body": "Description"
  }
}
```

### Получение репозитория

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "github_get_repo",
  "params": {
    "account": "my-github",
    "owner": "octocat",
    "repo": "Hello-World"
  }
}
```

## Структура проекта

```
GitHubMCPServer/
├── main.cpp                      # Точка входа
├── github_mcp_server.pro         # Файл проекта
├── accounts.json.example         # Шаблон конфигурации
├── docs/                         # Дополнительные документы
├── src/                          # Исходный код
└── tests/                        # Юнит-тесты
```

## Лицензия

MIT License

---

**Версия**: 1.0  
**Последнее обновление**: 2026-05-19