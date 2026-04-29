#ifndef EARSHIELD_UPDATE_CHECKER_H
#define EARSHIELD_UPDATE_CHECKER_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    void checkForUpdates(bool silentIfNone);

private slots:
    void onFeedFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_net;
    bool m_silentIfNone;
    bool m_inFlight;
};

#endif
