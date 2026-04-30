#include "UpdateChecker.h"
#include "version.h"
#include "updater_qt.h"
#include "debug_log.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QXmlStreamReader>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QTimer>
#include <QEventLoop>

#define EARSHIELD_FEED_URL "https://raw.githubusercontent.com/gamebaiters/EarShield/earshield/version.xml"

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_silentIfNone(true)
    , m_inFlight(false)
{
    ESLOG("UpdateChecker ctor: this=%p net=%p", (void*)this, (void*)m_net);
    connect(m_net, &QNetworkAccessManager::finished, this, &UpdateChecker::onFeedFinished);
}

UpdateChecker::~UpdateChecker() {
    ESLOG("UpdateChecker dtor: this=%p", (void*)this);
}

void UpdateChecker::checkForUpdates(bool silentIfNone)
{
    ESLOG("checkForUpdates(silent=%d) inFlight=%d", (int)silentIfNone, (int)m_inFlight);
    if (m_inFlight) return;
    m_silentIfNone = silentIfNone;
    m_inFlight = true;

    QNetworkRequest req{QUrl(QStringLiteral(EARSHIELD_FEED_URL))};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EarShield/%1").arg(EARSHIELD_VERSION_STRING));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    ESLOG("issuing GET %s", EARSHIELD_FEED_URL);
    m_net->get(req);
    ESLOG("GET issued");
}

void UpdateChecker::onFeedFinished(QNetworkReply* reply)
{
    ESLOG("onFeedFinished: error=%d (%s) bytes=%lld",
        (int)reply->error(),
        reply->errorString().toUtf8().constData(),
        (long long)reply->bytesAvailable());
    m_inFlight = false;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (!m_silentIfNone) {
            QMessageBox::warning(nullptr, QStringLiteral("EarShield"),
                QStringLiteral("Update check failed: %1").arg(reply->errorString()));
        }
        return;
    }

    int latestBuild = 0;
    QString latestStr;
    QString downloadUrl;
    QString featureUrl;

    QXmlStreamReader xml(reply->readAll());
    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;
        const auto name = xml.name();
        if (name == QLatin1String("latestVersion"))         latestBuild = xml.readElementText().trimmed().toInt();
        else if (name == QLatin1String("latestVersionString")) latestStr   = xml.readElementText().trimmed();
        else if (name == QLatin1String("url"))              downloadUrl = xml.readElementText().trimmed();
        else if (name == QLatin1String("featureUrl"))       featureUrl  = xml.readElementText().trimmed();
    }

    if (latestBuild <= 0 || downloadUrl.isEmpty()) {
        if (!m_silentIfNone) {
            QMessageBox::warning(nullptr, QStringLiteral("EarShield"),
                QStringLiteral("Update feed is malformed."));
        }
        return;
    }

    if (latestBuild <= EARSHIELD_VERSION_BUILD) {
        ESLOG("already up to date (installed=%d latest=%d) silent=%d",
            EARSHIELD_VERSION_BUILD, latestBuild, (int)m_silentIfNone);
        if (!m_silentIfNone) {
            ESLOG("about to show 'up to date' QMessageBox");
            QMessageBox::information(nullptr, QStringLiteral("EarShield"),
                QStringLiteral("EarShield %1 is up to date.").arg(EARSHIELD_VERSION_STRING));
            ESLOG("'up to date' QMessageBox returned");
        }
        return;
    }

    ESLOG("update available: latestBuild=%d latestStr=%s url=%s",
        latestBuild,
        latestStr.toUtf8().constData(),
        downloadUrl.toUtf8().constData());

    QMessageBox box;
    ESLOG("QMessageBox ctor ok");
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("EarShield update available"));
    box.setText(QStringLiteral("A new version of EarShield is available.\n\nInstalled: %1\nLatest: %2")
        .arg(EARSHIELD_VERSION_STRING, latestStr.isEmpty() ? QString::number(latestBuild) : latestStr));
    box.setInformativeText(QStringLiteral("Download and install now?\nTeamSpeak will be closed during the update."));
    QPushButton* updateBtn = box.addButton(QStringLiteral("Update now"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(updateBtn);

    if (!featureUrl.isEmpty()) {
        QPushButton* detailsBtn = box.addButton(QStringLiteral("Show details"), QMessageBox::ActionRole);
        QObject::connect(detailsBtn, &QPushButton::clicked, [featureUrl]() {
            QNetworkAccessManager nm;
            QEventLoop loop;
            QNetworkRequest req{QUrl(featureUrl)};
            QNetworkReply* r = nm.get(req);
            QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(8000, &loop, &QEventLoop::quit);
            loop.exec();
            QString notes = QString::fromUtf8(r->readAll());
            r->deleteLater();
            QMessageBox::information(nullptr, QStringLiteral("EarShield - Release notes"),
                notes.isEmpty() ? QStringLiteral("Release notes are not available.") : notes);
        });
    }

    ESLOG("box.exec() about to call");
    box.exec();
    ESLOG("box.exec() returned, clicked=%p update=%p", (void*)box.clickedButton(), (void*)updateBtn);
    if (box.clickedButton() != updateBtn) return;

    ESLOG("calling UpdaterQt::downloadAndInstall");
    UpdaterQt::downloadAndInstall(downloadUrl);
    ESLOG("downloadAndInstall returned");
}
