#include "src/GitHubMCPServer.h"
#include "src/TrafficLogger.h"
#include "src/ResponseCache.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("github_mcp_server");
    QCoreApplication::setApplicationVersion("2.0.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "C++/Qt MCP Server for GitHub API\n"
        "\n"
        "Supports GitHub.com and GitHub Enterprise.\n"
        "Communicates via JSON-RPC 2.0 over stdin/stdout.\n"
        "\n"
        "Example accounts.json for GitHub Enterprise:\n"
        "  {\n"
        "    \"accounts\": [{\n"
        "      \"name\": \"corp\",\n"
        "      \"type\": \"github_enterprise\",\n"
        "      \"username\": \"john\",\n"
        "      \"token\": \"ghp_...\",\n"
        "      \"baseUrl\": \"https://github.corp.com\"\n"
        "    }]\n"
        "  }");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption configOption({"c","config"},
        "Path to accounts.json config file.", "config");
    QCommandLineOption debugOption({"d","debug"},
        "Enable debug logging of MCP messages to stderr.");
    QCommandLineOption logOption({"l","log"},
        "Enable HTTP traffic logging. Levels: none, summary, headers, full.", "level", "none");
    QCommandLineOption logFileOption("log-file",
        "Write HTTP traffic log to this file (default: stderr).", "path");
    QCommandLineOption cacheOption("cache",
        "Enable in-memory response cache.");
    QCommandLineOption cacheTtlOption("cache-ttl",
        "Cache TTL in seconds (default: 60).", "seconds", "60");

    parser.addOption(configOption);
    parser.addOption(debugOption);
    parser.addOption(logOption);
    parser.addOption(logFileOption);
    parser.addOption(cacheOption);
    parser.addOption(cacheTtlOption);

    parser.process(app);

    // ── Traffic logger ────────────────────────────────────────────────────────
    QString logLevel = parser.value(logOption).toLower();
    TrafficLogger::LogLevel level = TrafficLogger::LogLevel::None;
    if      (logLevel == "summary") level = TrafficLogger::LogLevel::Summary;
    else if (logLevel == "headers") level = TrafficLogger::LogLevel::Headers;
    else if (logLevel == "full")    level = TrafficLogger::LogLevel::Full;

    gTrafficLogger.setLogLevel(level);
    if (parser.isSet(logFileOption))
        gTrafficLogger.setLogFile(parser.value(logFileOption));

    // ── Cache ─────────────────────────────────────────────────────────────────
    if (parser.isSet(cacheOption)) {
        gResponseCache.setEnabled(true);
        gResponseCache.setDefaultTtl(parser.value(cacheTtlOption).toInt());
    }

    // ── Server ────────────────────────────────────────────────────────────────
    GitHubMCPServer server;
    if (parser.isSet(configOption))
        server.setConfigPath(parser.value(configOption));
    if (parser.isSet(debugOption))
        server.setDebug(true);

    server.run();
    return app.exec();
}
