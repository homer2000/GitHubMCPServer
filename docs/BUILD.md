# Сборка проекта GitHub MCP Server

## Требования

### Минимальные требования

| Компонент | Версия | Примечание |
|-----------|--------|------------|
| **GCC** | 7.0+ | C++17 поддержка |
| **Qt** | 5.15.2 / 6.5+ | Qt Network, Qt Core |
| **make** | Любой | Стандартный make |

### Проверка окружения

```bash
# Проверить GCC версию
g++ --version
# Требуется: GCC 7.0+

# Проверить Qt установку
/opt/Qt/5.15.2/gcc_64/bin/qmake -v
# или для Qt 6.x:
/opt/Qt/6.x.x/gcc_64/bin/qmake -v

# Проверить OpenSSL (опционально)
openssl version
```

## Варианты сборки

### Вариант 1: Автоматическая сборка через MCP инструменты

```bash
cd /home/homer/QT/neuralNetwork/Export_Projects/GitHubMCPServer

# Очистка каталога сборки
mcp__qt-build__clean_build_dir --project_dir $(pwd)

# Сборка с Qt 6.10.2 (рекомендуется)
mcp__qt-build__build_qt6_10_2_gcc_64 --project_dir $(pwd)

# ИЛИ другой комплект:
# mcp__qt-build__build_qt5_15_2_gcc_64 --project_dir $(pwd)
# mcp__qt-build__build_qt6_5_1_gcc_64 --project_dir $(pwd)
# mcp__qt-build__build_qt6_7_2_gcc_64 --project_dir $(pwd)
```

### Вариант 2: Ручная сборка через qmake/make

#### Подготовка окружения

```bash
# Установите переменные окружения для Qt 6.10.2
export QTDIR="/opt/Qt/6.10.2/gcc_64"
export PATH="$QTDIR/bin:$PATH"
export LD_LIBRARY_PATH="$QTDIR/lib:$LD_LIBRARY_PATH"

# Проверьте установку
qmake -v
```

#### Создание Makefile

```bash
cd /home/homer/QT/neuralNetwork/Export_Projects/GitHubMCPServer

# Создание каталога сборки
mkdir -p build
cd build

# Генерация Makefile
qmake ../github_mcp_server.pro

# Проверка сгенерированных файлов
ls -la Makefile config.*
```

#### Сборка

```bash
# Сборка (используйте все ядра)
make -j$(nproc)

# ИЛИ с указанием количества потоков
make -j4
make -j8
```

#### Очистка

```bash
# Очистка объектных файлов
make clean

# Полная очистка (удаление Makefile)
make distclean
rm -rf build/
```

## Результаты сборки

### Успешная сборка

```bash
# Проверка исполняемого файла
ls -la build/github_mcp_server

# Размер бинарника
-rwxrwxr-x 1 homer homer 789400 May 19 03:43 github_mcp_server
```

### Проверка работы

```bash
# Справка
./build/github_mcp_server --help

# Тестовый запуск (без конфигурации)
./build/github_mcp_server
```

## Сравнение Qt версий

| Qt Версия | Статус | Предупреждения | Размер | Рекомендация |
|-----------|--------|----------------|--------|--------------|
| **Qt 5.15.2** | ✅ Успешно | 2 (C++17 ext) | 840KB | LTS, стабильный |
| **Qt 6.5.1** | ✅ Успешно | 0 | 789KB | Современный, стабольный |
| **Qt 6.7.2** | ✅ Успешно | 0 | ~790KB | Актуальный |
| **Qt 6.10.2** | ✅ Успешно | 0 | ~790KB | **Рекомендуется** |

### Рекомендация по версиям

1. **Для продакшена**: Qt 6.5.1 (стабольная версия)
2. **Для разработки**: Qt 6.10.2 (последняя версия)
3. **Для совместимости**: Qt 5.15.2 (LTS)

## Отладка сборки

### Включение отладочной информации

```bash
# Создание Makefile с отладкой
qmake CONFIG+=debug github_mcp_server.pro

# Сборка отладочной версии
make

# Запуск под отладчиком
gdb build/github_mcp_server
```

### Анализ предупреждений

```bash
# Полный вывод компиляции
make 2>&1 | tee build.log

# Фильтрация предупреждений
grep "warning:" build.log
```

## Проблемы и решения

### Ошибка: "qmake: command not found"

```bash
# Добавить Qt в PATH
export PATH="/opt/Qt/6.10.2/gcc_64/bin:$PATH"

# Постоянное добавление
echo 'export PATH="/opt/Qt/6.10.2/gcc_64/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### Ошибка: "Qt5Core: No such file or directory"

```bash
# Проверить установку Qt
ls /opt/Qt/5.15.2/gcc_64/lib/libQt5*.so

# Добавить библиотеки в PATH
export LD_LIBRARY_PATH="/opt/Qt/5.15.2/gcc_64/lib:$LD_LIBRARY_PATH"
```

### Ошибка: "c++17 feature not available"

```bash
# Явное указание флага C++17
qmake CONFIG+=c++17 github_mcp_server.pro
make
```

### Ошибка: "Permission denied"

```bash
# Дать права на выполнение
chmod +x build/github_mcp_server
```

## Сборка в Docker (будущое)

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    g++ \
    qtbase5-dev \
    qmake \
    make

COPY . /app
WORKDIR /app

RUN qmake github_mcp_server.pro && make -j$(nproc)
```

## CI/CD интеграция

### GitHub Actions

```yaml
name: Build

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup Qt
        uses: jurplel/install-qt-action@v3
        with:
          version: 6.5.1
      
      - name: Build
        run: |
          qmake github_mcp_server.pro
          make -j$(nproc)
      
      - name: Test
        run: |
          ./build/github_mcp_server --help
```

---

**Версия**: 1.0  
**Последнее обновление**: 2026-05-19