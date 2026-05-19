# ─────────────────────────────────────────────────────────────────────────────
# GitHub MCP Server — universal qmake project (Qt 5 and Qt 6)
#
# Build:
#   qmake github_mcp_server.pro && make
#
# Force Qt version explicitly:
#   qmake github_mcp_server.pro QT_MAJOR_VERSION=5
#   qmake github_mcp_server.pro QT_MAJOR_VERSION=6
# ─────────────────────────────────────────────────────────────────────────────

QT += core network
QT -= gui

TEMPLATE = app
TARGET   = github_mcp_server

CONFIG -= app_bundle
CONFIG += console

INCLUDEPATH += src

# ── C++ standard ─────────────────────────────────────────────────────────────
# Qt 6 requires C++17. Qt 5 supports it from 5.7+.
greaterThan(QT_MAJOR_VERSION, 5) {
    CONFIG += c++17
} else {
    CONFIG += c++14
    # Enable C++17 features via compiler flag on Qt 5 (GCC/Clang)
    !msvc: QMAKE_CXXFLAGS += -std=c++17
}

# ── Qt 5 specific ─────────────────────────────────────────────────────────────
equals(QT_MAJOR_VERSION, 5) {
    # QTextStream::setCodec is available in Qt 5, removed in Qt 6
    DEFINES += USE_QTEXTSTREAM_CODEC
    # QString::split(char, QString::SkipEmptyParts) — available in Qt 5
    DEFINES += QT5_COMPAT
}

# ── Qt 6 specific ─────────────────────────────────────────────────────────────
greaterThan(QT_MAJOR_VERSION, 5) {
    # Qt 6: QTextStream uses UTF-8 by default, setCodec removed
    # Qt 6: QString::split uses Qt::SkipEmptyParts enum
    DEFINES += QT6_COMPAT
}

# ── Sources ───────────────────────────────────────────────────────────────────
SOURCES += \
    main.cpp \
    src/GitHubAccount.cpp \
    src/GitHubAPI.cpp \
    src/GitHubMCPServer.cpp \
    src/TrafficLogger.cpp \
    src/ResponseCache.cpp \
    src/QMCPServer.cpp

HEADERS += \
    src/GitHubAccount.h \
    src/GitHubAPI.h \
    src/GitHubMCPServer.h \
    src/TrafficLogger.h \
    src/ResponseCache.h \
    src/QMCPServer.h

# ── Tests ─────────────────────────────────────────────────────────────────────
TESTS += tst_githubaccount

# ── Compiler warnings ─────────────────────────────────────────────────────────
QMAKE_CXXFLAGS += -Wall -Wextra

# ── Install ───────────────────────────────────────────────────────────────────
unix {
    target.path = /usr/local/bin
    INSTALLS += target
}

# ── Info ──────────────────────────────────────────────────────────────────────
message("Qt version: $$QT_VERSION (major: $$QT_MAJOR_VERSION)")
message("Target:     $$TARGET")
message("Build dir:  $$OUT_PWD")
