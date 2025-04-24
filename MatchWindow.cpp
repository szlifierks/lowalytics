#include "MatchWindow.h"
#include <QCloseEvent>
#include <QApplication>
#include <QScreen>
#include <QProcess>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QEventLoop>
#include <QSslConfiguration>


MatchWindow::MatchWindow(QWidget *parent) : QWidget(parent), currentState(WAITING), overlayEnabled(false), last(WAITING) {
    avatarLabel = new QLabel;
    summonerLabel = new QLabel;
    rankInfoLabel = new QLabel;
    matchHistoryList = new QListWidget;
    queueSelector = new QComboBox;

    auto *mainLayout = new QVBoxLayout(this);

    stackedWidget = new QStackedWidget(this);

    // --- Widok oczekiwania ---
    waitingWidget = new QWidget;
    QVBoxLayout *waitingLayout = new QVBoxLayout(waitingWidget);

    //avatar, nick, ranga
    QHBoxLayout *topLayout = new QHBoxLayout;

    avatarLabel->setFixedSize(64, 64);
    avatarLabel->setPixmap(QPixmap());
    summonerLabel->setText("");

    QVBoxLayout *rankLayout = new QVBoxLayout;
    queueSelector->addItems({"soloq", "flex"});
    connect(queueSelector, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx == 0)
            obtainInfo("RANKED_SOLO_5x5");
        else
            obtainInfo("RANKED_FLEX_SR");
    });

    rankInfoLabel->setText("");
    rankLayout->addWidget(queueSelector);
    rankLayout->addWidget(rankInfoLabel);

    topLayout->addWidget(avatarLabel);
    topLayout->addWidget(summonerLabel);
    topLayout->addLayout(rankLayout);

    waitingLayout->addLayout(topLayout);
    waitingLayout->addWidget(matchHistoryList);

    // --- Widok champselect ---
    champSelectWidget = new QWidget(this);
    auto *matchLayout = new QVBoxLayout(champSelectWidget);
    auto *matchStatsLabel = new QLabel("Ekran wyboru postaci", champSelectWidget);
    matchLayout->addWidget(matchStatsLabel);

    // --- Widok ingame overlay ---
    inGameWidget = new QWidget(this);
    auto *inGameLayout = new QVBoxLayout(inGameWidget);
    auto *inGameLabel = new QLabel("Overlay aktywny", inGameWidget);
    inGameLayout->addWidget(inGameLabel);

    stackedWidget->addWidget(waitingWidget);
    stackedWidget->addWidget(champSelectWidget);
    stackedWidget->addWidget(inGameWidget);

    mainLayout->addWidget(stackedWidget);

    networkManager = new QNetworkAccessManager(this);

    createTrayIcon();

    gameStateTimer = new QTimer(this);
    connect(gameStateTimer, &QTimer::timeout, this, &MatchWindow::checkGameState);
    gameStateTimer->start(2000);

    switchToState(WAITING);
}

MatchWindow::~MatchWindow() {
    delete trayIcon;
    delete trayIconMenu;
    // network manager sam sie usunie
}

void MatchWindow::createTrayIcon() {
    trayIconMenu = new QMenu(this);

    showAction = new QAction("show main", this);
    connect(showAction, &QAction::triggered, this, &MatchWindow::showNormal);

    toggleOverlayAction = new QAction("turn on overlay", this);
    connect(toggleOverlayAction, &QAction::triggered, this, &MatchWindow::toggleOverlay);

    quitAction = new QAction("close", this);
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    trayIconMenu->addAction(showAction);
    trayIconMenu->addAction(toggleOverlayAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);

    // tray icon
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QIcon(":/icons/image.png"));
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip("Lowalytics");

    connect(trayIcon, &QSystemTrayIcon::activated, this, &MatchWindow::iconActivated);

    trayIcon->show();
}

void MatchWindow::closeEvent(QCloseEvent *event) {
    if (trayIcon->isVisible()) {
        hide();
        event->ignore();
    }
}

void MatchWindow::iconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        if (isVisible()) {
            hide();
        } else {
            showNormal();
            activateWindow();
            raise();
        }
    }
}

void MatchWindow::toggleOverlay() {
    overlayEnabled = !overlayEnabled;

    if (overlayEnabled) {
        toggleOverlayAction->setText("Wyłącz overlay");
        if (currentState == IN_GAME) {
            setupOverlay();
        }
    } else {
        toggleOverlayAction->setText("Włącz overlay");
        setWindowFlags(Qt::Window);
        show();
    }
}

void MatchWindow::showNormal() {
    if (currentState != IN_GAME || !overlayEnabled) {
        QWidget::showNormal();
        activateWindow();
        raise();
    }
}

void MatchWindow::setupOverlay() {

    // overlay
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);

    setWindowOpacity(0.8);

    QScreen *screen = QApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    int width = 300;
    int height = 200;
    int x = screenGeometry.width() - width - 20;
    int y = 50;

    setGeometry(x, y, width, height);

    show();
}

bool MatchWindow::findRiotClientCredentials() {
    QStringList potentialPaths;

    potentialPaths << "C:/Riot Games/League of Legends/lockfile"
            << "D:/Riot Games/League of Legends/lockfile"
            << "C:/Program Files/Riot Games/League of Legends/lockfile"
            << "C:/Program Files (x86)/Riot Games/League of Legends/lockfile";

    QString lockfilePath;
    for (const QString &path: potentialPaths) {
        if (QFile::exists(path)) {
            lockfilePath = path;
            break;
        }
    }

    if (lockfilePath.isEmpty()) {
        qDebug() << "nie znaleziono pliku lockfile";
        return false;
    }

    QFile lockfile(lockfilePath);
    if (!lockfile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "nie mozna otworzyc pliku lockfile";
        return false;
    }

    QString lockfileContent = QString::fromUtf8(lockfile.readAll());
    lockfile.close();

    QStringList parts = lockfileContent.split(':');
    if (parts.size() < 4) {
        qDebug() << "nieprawidlowy format pliku lockfile";
        return false;
    }

    riotClientPort = parts[2];
    riotClientToken = parts[3];

    qDebug() << "znaleziono klienta - port:" << riotClientPort;
    obtainInfo("RANKED_SOLO_5x5");
    return true;
}

void MatchWindow::checkGameState() {
    gameStateTimer->stop();

    GameState last = currentState;


    if (riotClientPort.isEmpty() || riotClientToken.isEmpty()) {
        if (!findRiotClientCredentials()) {
            switchToState(last);
            gameStateTimer->start(2000); // uruchom timer ponownie
            return;
        }
    }

    // API request
    QNetworkRequest request(QUrl("https://127.0.0.1:" + riotClientPort + "/lol-gameflow/v1/gameflow-phase"));
    request.setRawHeader("Authorization", "Basic " + QByteArray(("riot:" + riotClientToken).toUtf8()).toBase64());
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = networkManager->get(request);

    QTimer *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, [reply]() {
        if (!reply->isFinished())
            reply->abort();
    });
    timeoutTimer->start(2000); // 2 sekundy timeout

    connect(reply, &QNetworkReply::finished, this, [this, reply, timeoutTimer, last]() {
        timeoutTimer->stop();
        timeoutTimer->deleteLater();

        if (reply->error() == QNetworkReply::NoError) {
            QString phase = QString::fromUtf8(reply->readAll()).replace("\"", "");
            qDebug() << "aktualna faza: " << phase;

            if (phase == "InProgress") {
                switchToState(IN_GAME);
            } else if (phase == "ChampSelect") {
                switchToState(CHAMP_SELECT);
            } else {
                switchToState(WAITING);
            }
        } else {
            qDebug() << "błąd z LCU API: " << reply->errorString();
            riotClientPort.clear();
            riotClientToken.clear();
            switchToState(last);
        }

        reply->deleteLater();

        gameStateTimer->start(2000);
    });
}

void MatchWindow::switchToState(GameState state) {
    if (state == currentState) {
        return;
    }

    currentState = state;

    switch (state) {
        case WAITING:
            stackedWidget->setCurrentWidget(waitingWidget);
            setWindowOpacity(1.0);
            if (isHidden() && !overlayEnabled) {
                show();
            }
            break;

        case CHAMP_SELECT:
            stackedWidget->setCurrentWidget(champSelectWidget);
            setWindowOpacity(1.0);
            if (isHidden()) {
                show();
            }
            break;

        case IN_GAME:
            stackedWidget->setCurrentWidget(inGameWidget);
            trayIcon->showMessage("Lowalytics", "match started", QIcon(), 3000);

            if (overlayEnabled) {
                setupOverlay();
            } else {
                hide();
            }
            break;
    }
}

// MatchWindow.cpp

void MatchWindow::obtainInfo(QString qType) {
    QNetworkRequest summonerReq(QUrl("https://127.0.0.1:" + riotClientPort + "/lol-summoner/v1/current-summoner"));
    summonerReq.setRawHeader("Authorization", "Basic " + QByteArray(("riot:" + riotClientToken).toUtf8()).toBase64());
    QNetworkReply *summonerReply = networkManager->get(summonerReq);
    connect(summonerReply, &QNetworkReply::finished, this, [this, summonerReply]() {
        if (summonerReply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(summonerReply->readAll()).object();
            QString gameName = obj["gameName"].toString();
            QString tagLine = obj["tagLine"].toString();
            QString riotID = gameName + "#" + tagLine;
            int iconId = obj["profileIconId"].toInt();
            QString avatarUrl = QString("http://ddragon.leagueoflegends.com/cdn/14.12.1/img/profileicon/%1.png").arg(iconId);

            summonerLabel->setText(riotID);

            QNetworkRequest avatarReq{QUrl(avatarUrl)};
            QNetworkReply *avatarReply = networkManager->get(avatarReq);
            connect(avatarReply, &QNetworkReply::finished, this, [this, avatarReply]() {
                if (avatarReply->error() == QNetworkReply::NoError) {
                    QPixmap pix;
                    pix.loadFromData(avatarReply->readAll());
                    avatarLabel->setPixmap(pix.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                }
                avatarReply->deleteLater();
            });
        }
        summonerReply->deleteLater();
    });

    QNetworkRequest rankedReq(QUrl("https://127.0.0.1:" + riotClientPort + "/lol-ranked/v1/current-ranked-stats"));
    rankedReq.setRawHeader("Authorization", "Basic " + QByteArray(("riot:" + riotClientToken).toUtf8()).toBase64());
    QNetworkReply *rankedReply = networkManager->get(rankedReq);
    connect(rankedReply, &QNetworkReply::finished, this, [this, rankedReply, qType]() {
        if (rankedReply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(rankedReply->readAll()).object();
            QJsonArray queues = obj["queues"].toArray();
            for (const QJsonValue &queueVal : queues) {
                QJsonObject queue = queueVal.toObject();
                if (queue["queueType"] == qType) {
                    QString tier = queue["tier"].toString();
                    QString division = queue["division"].toString();
                    int wins = queue["wins"].toInt();
                    int losses = queue["losses"].toInt();
                    rankInfoLabel->setText(QString("%1 %2 (%3W/%4L)").arg(tier, division).arg(wins).arg(losses));
                }
            }
        }
        rankedReply->deleteLater();
    });

    QNetworkRequest historyReq(QUrl("https://127.0.0.1:" + riotClientPort + "/lol-match-history/v1/products/lol/current-summoner/matches"));
    historyReq.setRawHeader("Authorization", "Basic " + QByteArray(("riot:" + riotClientToken).toUtf8()).toBase64());
    QNetworkReply *historyReply = networkManager->get(historyReq);
    connect(historyReply, &QNetworkReply::finished, this, [this, historyReply]() {
        if (historyReply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(historyReply->readAll()).object();
            QJsonArray games = obj["games"].toArray();
            matchHistoryList->clear();
            for (const QJsonValue &gameVal : games) {
                QJsonObject game = gameVal.toObject();
                QString champ = game["championName"].toString();
                int kills = game["kills"].toInt();
                int deaths = game["deaths"].toInt();
                int assists = game["assists"].toInt();
                bool win = game["win"].toBool();
                QString result = win ? "Win" : "Loss";
                matchHistoryList->addItem(QString("%1 - %2 - %3/%4/%5").arg(champ, result).arg(kills).arg(deaths).arg(assists));
            }
        }
        historyReply->deleteLater();
        qDebug() << "historia: "<< matchHistoryList->count();
    });
}
