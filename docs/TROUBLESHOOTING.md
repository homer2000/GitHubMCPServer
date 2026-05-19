# Устранение неполадок GitHub MCP Server

## Содержание

1. [Проблемы со сборкой](#проблемы-со-сборкой)
2. [Проблемы запуска](#проблемы-запуска)
3. [Проблемы с сетью](#проблемы-сетью)
4. [Проблемы с API](#проблемы-с-api)
5. [Логи и диагностика](#логи-и-диагностика)

---

## Проблемы со сборкой

### Ошибка: "qmake: command not found"

**Симптом**:
```
bash: qmake: command not found
```

**Решение**:
```bash
# Добавить Qt в PATH
export PATH="/opt/Qt/6.10.2/gcc_64/bin:$PATH"

# Проверить установку
qmake -v

# Постоянное решение
echo 'export PATH="/opt/Qt/6.10.2/gcc_64/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### Ошибка: "Qt5Core: No such file or directory"

**Симптом**:
```
fatal error: Qt5Core/QtCore: No such file or directory
```

**Решение**:
```bash
# Проверить установку Qt
ls /opt/Qt/5.15.2/gcc_64/include/QtCore

# Добавить include пути
export CPLUS_INCLUDE_PATH="/opt/Qt/5.15.2/gcc_64/include:$CPLUS_INCLUDE_PATH"
export LIBRARY_PATH="/opt/Qt/5.15.2/gcc_64/lib:$LIBRARY_PATH"

# Или используйте LD_LIBRARY_PATH
export LD_LIBRARY_PATH="/opt/Qt/5.15.2/gcc_64/lib:$LD_LIBRARY_PATH"
```

### Ошибка компиляции: "c++17 feature not available"

**Симптом**:
```
error: 'if constexpr' only available with '-std=c++17'
```

**Решение**:
```bash
# Явное указание C++17 в qmake
qmake CONFIG+=c++17 github_mcp_server.pro
make
```

### Ошибка: "Permission denied" при сборке

**Симптом**:
```
make: *** [build/Makefile] Error 13
```

**Решение**:
```bash
# Проверить права на запись
ls -la build/

# Дать права
chmod u+w build/
# или
rm -rf build/
mkdir build/
```

### Ошибка linking: "undefined reference"

**Симптом**:
```
undefined reference to `QNetworkAccessManager::...'
```

**Решение**:
```bash
# Проверить, подключены ли модули в .pro файле
# Должно быть:
QT += network core

# Пересобрать
make clean
qmake github_mcp_server.pro
make
```

---

## Проблемы запуска

### Сервер не отвечает

**Симптом**: Сервер запущен, но не обрабатывает запросы.

**Диагностика**:
```bash
# Проверить, слушает ли процесс порт
ps aux | grep github_mcp_server

# Проверить вывод сервера
./build/github_mcp_server --log-level full 2>&1 | tee server.log
```

**Решение**:
```bash
# Убедиться, что конфигурация корректна
./build/github_mcp_server --config accounts.json --log-level full
```

### Ошибка: "config file not found"

**Симптом**:
```
Error: Cannot open config file
```

**Решение**:
```bash
# Проверить путь к файлу
ls -la accounts.json

# Указать абсолютный путь
./build/github_mcp_server --config /full/path/to/accounts.json
```

### Ошибка: "Invalid JSON в ответе"

**Симптом**: Сервер возвращает мусор.

**Решение**:
```bash
# Проверить кодировку
file build/github_mcp_server

# Пересобрать
make clean && make
```

### Сервер падает при запуске

**Симптом**: Сигналы SIGSEGV, SIGABRT.

**Диагностика**:
```bash
# Запуск под отладчиком
gdb build/github_mcp_server

# В gdb:
(gdb) run
# ... падение ...
(gdb) bt
```

**Решение**:
```bash
# Проверить конфигурацию
cat accounts.json | python3 -m json.tool

# Проверить токены
# Убедиться, что токены валидны
```

---

## Проблемы с сетью

### Ошибка: "Could not resolve host"

**Симптом**:
```
Error: Could not resolve host: api.github.com
```

**Решение**:
```bash
# Проверить DNS
nslookup api.github.com

# Проверить интернет
ping github.com

# Проверить прокси
env | grep -i proxy
```

### Ошибка: "Connection refused"

**Симптом**:
```
Error: Connection refused
```

**Решение**:
```bash
# Проверить доступность хоста
telnet api.github.com 443

# Проверить фаервол
sudo iptables -L

# Проверить прокси
export HTTP_PROXY=http://proxy:port
export HTTPS_PROXY=http://proxy:port
```

### Ошибка: "SSL handshake failed"

**Симптом**:
```
SSL handshake failed
```

**Решение**:
```bash
# Обновить сертификаты
sudo update-ca-certificates

# Проверить версию OpenSSL
openssl version

# Отключить проверку SSL (только для тестирования!)
export QT_NETWORK_SSL_NO_PROXY=true
```

### Rate limit exceeded

**Симптом**:
```json
{
  "message": "API rate limit exceeded"
}
```

**Решение**:
```bash
# Проверить лимит
curl -H "Authorization: token YOUR_TOKEN" https://api.github.com/rate_limit

# Подождать 1 час или использовать другой токен
```

---

## Проблемы с API

### Ошибка 401: "Bad credentials"

**Симптом**:
```json
{
  "message": "Bad credentials"
}
```

**Решение**:
```bash
# Проверить токен
curl -H "Authorization: token YOUR_TOKEN" https://api.github.com/user

# Проверить права токена
# Токен должен содержать scope: repo, read:org

# Обновить токен в accounts.json
```

### Ошибка 403: "Forbidden"

**Симптом**:
```json
{
  "message": "Forbidden"
}
```

**Возможные причины**:
1. Недостаточно прав у токена
2. Репозиторий приватный
3. Организация требует дополнительные права

**Решение**:
```bash
# Проверить права
curl -H "Authorization: token YOUR_TOKEN" https://api.github.com/user/repos

# Добавить права токену
```

### Ошибка 404: "Not Found"

**Симптом**:
```json
{
  "message": "Not Found"
}
```

**Решение**:
```bash
# Проверить имя репозитория
# Проверить права доступа

# Использовать полное имя репозитория
{
  "owner": "organization",
  "repo": "repository"
}
```

### Ошибка 422: "Unprocessable Entity"

**Симптом**:
```json
{
  "message": "Validation Failed",
  "errors": [...]
}
```

**Решение**:
```bash
# Проверить параметры запроса
# Убедиться, что все обязательные поля заполнены

# Проверить список ошибок в ответе
```

---

## Логи и диагностика

### Настройка уровня логирования

```bash
# Полный лог (все запросы/ответы)
./build/github_mcp_server --log-level full

# Заголовки запросов/ответов
./build/github_mcp_server --log-level headers

# Краткий итог
./build/github_mcp_server --log-level summary

# Без логов
./build/github_mcp_server --log-level none
```

### Анализ логов

```bash
# Сохранить логи
./build/github_mcp_server --log-level full 2>&1 | tee server.log

# Фильтрация ошибок
grep -i error server.log

# Фильтрация запросов
grep "POST\|GET" server.log

# Подсчет запросов
grep "POST" server.log | wc -l
```

### Мониторинг производительности

```bash
# Мониторить использование CPU
top -p $(pgrep github_mcp_server)

# Мониторить сеть
iftop -P $(pgrep github_mcp_server)

# Профилирование памяти
valgrind --tool=massif build/github_mcp_server
```

---

## FAQ

### Q: Как проверить, что токен работает?

```bash
curl -H "Authorization: token YOUR_TOKEN" https://api.github.com/user
```

### Q: Сколько запросов в час можно сделать?

Стандартный лимит: 5000 запросов/час для аутентифицированных запросов.

### Q: Можно ли использовать GraphQL?

В текущей версии поддерживается только REST API.

### Q: Как обновить зависимости?

```bash
# Пересобрать проект
make clean
make
```

### Q: Где хранятся логи?

Логи выводятся в stderr. Перенаправьте их в файл:
```bash
./build/github_mcp_server > stdout.log 2> stderr.log
```

---

**Версия**: 1.0  
**Последнее обновление**: 2026-05-19