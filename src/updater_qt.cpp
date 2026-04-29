#include "updater_qt.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QMessageBox>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>

namespace {

bool writeWindowsHelper(const QString& packagePath)
{
    const QString helperPath = QDir::temp().absoluteFilePath(QStringLiteral("earshield_update_helper.bat"));
    QFile helper(helperPath);
    if (!helper.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    const QString tplugin = QDir::toNativeSeparators(packagePath);
    QTextStream out(&helper);
    out << "@echo off\r\n";
    out << "timeout /t 2 /nobreak >nul\r\n";
    out << "taskkill /F /IM ts3client_win64.exe >nul 2>&1\r\n";
    out << "taskkill /F /IM ts3client_win32.exe >nul 2>&1\r\n";
    out << "timeout /t 1 /nobreak >nul\r\n";
    // legacy binary names — keep config (EarShield.ini) intact
    out << "del /F /Q \"%APPDATA%\\TS3Client\\plugins\\EarShield_win64.dll\" 2>nul\r\n";
    out << "del /F /Q \"%APPDATA%\\TS3Client\\plugins\\EarShield_win32.dll\" 2>nul\r\n";
    out << "del /F /Q \"%APPDATA%\\TS3Client\\plugins\\volumeleveler_win64.dll\" 2>nul\r\n";
    out << "del /F /Q \"%APPDATA%\\TS3Client\\plugins\\volumeleveler_win32.dll\" 2>nul\r\n";
    out << "start \"\" \"" << tplugin << "\"\r\n";
    helper.close();
    return QProcess::startDetached(QStringLiteral("cmd.exe"), QStringList() << QStringLiteral("/c") << helperPath);
}

bool writeUnixHelper(const QString& packagePath)
{
    const QString helperPath = QDir::temp().absoluteFilePath(QStringLiteral("earshield_update_helper.sh"));
    QFile helper(helperPath);
    if (!helper.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    QTextStream out(&helper);
    out << "#!/usr/bin/env bash\nset -u\n";
    out << "PACKAGE='" << packagePath << "'\n";
    out << "sleep 2\n";
#ifdef Q_OS_MAC
    out << "osascript -e 'tell application \"TeamSpeak 3\" to quit' 2>/dev/null || true\n";
    out << "sleep 1\n";
    out << "pkill -9 -x ts3client 2>/dev/null || true\n";
    out << "pkill -9 -if 'TeamSpeak 3' 2>/dev/null || true\n";
    out << "sleep 1\n";
    out << "TARGET_BASE=''\n";
    out << "for cand in \"$HOME/Library/Application Support/TeamSpeak 3\" \\\n";
    out << "            \"$HOME/Library/Application Support/TS3Client\" \\\n";
    out << "            \"$HOME/.ts3client\"; do\n";
    out << "    if [ -d \"$cand\" ]; then TARGET_BASE=\"$cand\"; break; fi\n";
    out << "done\n";
    out << "[ -z \"$TARGET_BASE\" ] && TARGET_BASE=\"$HOME/Library/Application Support/TeamSpeak 3\"\n";
    out << "PLUGIN_DIR=\"$TARGET_BASE/plugins\"\n";
    out << "mkdir -p \"$PLUGIN_DIR\"\n";
    out << "for lib in libearshield_mac.dylib earshield_mac.dylib libearshield.dylib libvolumeleveler_mac.dylib; do\n";
    out << "    rm -f \"$PLUGIN_DIR/$lib\" 2>/dev/null\n";
    out << "done\n";
    // Stale bundled Qt frameworks from a previous install would shadow the
    // new ones via @loader_path/Frameworks. Wipe them before extracting.
    out << "for q in Core Gui Network Widgets DBus PrintSupport; do\n";
    out << "    rm -rf \"$PLUGIN_DIR/Frameworks/Qt${q}.framework\" 2>/dev/null\n";
    out << "done\n";
    out << "TMP=\"$(mktemp -d)\"\n";
    out << "trap 'rm -rf \"$TMP\"' EXIT\n";
    out << "/usr/bin/ditto -x -k \"$PACKAGE\" \"$TMP\"\n";
    out << "if [ -d \"$TMP/plugins\" ]; then\n";
    out << "    cp -R \"$TMP/plugins/.\" \"$PLUGIN_DIR/\"\n";
    out << "    find \"$PLUGIN_DIR\" -maxdepth 1 -name 'libearshield*.dylib' -exec xattr -dr com.apple.quarantine {} + 2>/dev/null || true\n";
    out << "fi\n";
    out << "open -a 'TeamSpeak 3' 2>/dev/null || true\n";
#else
    out << "pkill -9 -x ts3client_linux_amd64 2>/dev/null || true\n";
    out << "pkill -9 -x ts3client_linux_x86 2>/dev/null || true\n";
    out << "sleep 1\n";
    out << "for base in \"$HOME/.ts3client\"; do\n";
    out << "    [ -d \"$base/plugins\" ] || continue\n";
    out << "    for lib in libearshield_linux_amd64.so earshield_linux_amd64.so \\\n";
    out << "               libvolumeleveler_linux_amd64.so libvolumeleveler_it_linux_amd64.so; do\n";
    out << "        rm -f \"$base/plugins/$lib\" 2>/dev/null\n";
    out << "    done\n";
    out << "done\n";
    out << "if command -v package_inst >/dev/null 2>&1; then\n";
    out << "    package_inst \"$PACKAGE\"\n";
    out << "elif command -v xdg-open >/dev/null 2>&1; then\n";
    out << "    xdg-open \"$PACKAGE\"\n";
    out << "fi\n";
#endif
    helper.close();
    helper.setPermissions(helper.permissions() | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther);
    return QProcess::startDetached(QStringLiteral("/bin/bash"), QStringList() << helperPath);
}

}

namespace UpdaterQt {

void downloadAndInstall(const QString& url)
{
    QString suggested = QFileInfo(QUrl(url).path()).fileName();
    if (suggested.isEmpty()) suggested = QStringLiteral("EarShield.ts3_plugin");
    const QString outPath = QDir::temp().absoluteFilePath(suggested);

    QNetworkAccessManager nm;
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nm.get(req);

    QProgressDialog progress(QStringLiteral("Downloading EarShield update..."), QStringLiteral("Cancel"), 0, 100);
    progress.setWindowTitle(QStringLiteral("EarShield"));
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 got, qint64 total) {
        if (total > 0) progress.setValue(int(100 * got / total));
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::warning(nullptr, QStringLiteral("EarShield"),
            QStringLiteral("Download failed: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(nullptr, QStringLiteral("EarShield"),
            QStringLiteral("Cannot write %1").arg(outPath));
        reply->deleteLater();
        return;
    }
    out.write(reply->readAll());
    out.close();
    reply->deleteLater();

#ifdef Q_OS_WIN
    const bool ok = writeWindowsHelper(outPath);
#else
    const bool ok = writeUnixHelper(outPath);
#endif
    if (!ok) {
        QMessageBox::warning(nullptr, QStringLiteral("EarShield"),
            QStringLiteral("Failed to launch updater helper."));
    }
}

}
