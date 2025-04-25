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
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QPainter>
#include <QPainterPath>

MatchWindow::MatchWindow(QWidget *parent) : QWidget(parent), currentState(WAITING), overlayEnabled(false), last(WAITING) {
//styles etc
    QApplication::setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(30, 30, 30));
    palette.setColor(QPalette::WindowText, QColor(220, 220, 220));
    palette.setColor(QPalette::Base, QColor(40, 40, 40));
    palette.setColor(QPalette::AlternateBase, QColor(50, 50, 50));
    palette.setColor(QPalette::Button, QColor(60, 60, 60));
    palette.setColor(QPalette::ButtonText, QColor(220, 220, 220));
    QApplication::setPalette(palette);

    setStyleSheet(R"(
    QWidget {
        background: #1e1e1e;
        border-radius: 16px;
    }
    QLabel#header {
        font-size: 20px;
        font-weight: bold;
        color: #fff;
        margin-bottom: 12px;
    }
    QListWidget {
        background: #2c2c2c;
        border: 1px solid #3c3c3c;
        border-radius: 8px;
        font-size: 14px;
        padding: 8px;
        color: #dddddd;
    }
    QListWidget::item {
        margin: 6px 0;
        padding: 6px;
        border-radius: 6px;
    }
    QListWidget::item:selected {
        background: #444;
        color: #fff;
    }
    QComboBox {
        background: #2c2c2c;
        border-radius: 6px;
        padding: 4px 12px;
        font-size: 14px;
        color: #ffffff;
    }
    QLabel {
        font-size: 15px;
        color: #ffffff;
    }
    QLabel#playerName {
        font-size: 18px;
    }
)");

    // Prosta animacja fade-in
    QPropertyAnimation *fadeAnimation = new QPropertyAnimation(this, "windowOpacity", this);
    fadeAnimation->setDuration(700);
    fadeAnimation->setStartValue(0.0);
    fadeAnimation->setEndValue(1.0);
    fadeAnimation->start(QAbstractAnimation::DeleteWhenStopped);

    avatarLabel = new QLabel;
    summonerLabel = new QLabel;
    summonerLabel->setObjectName("playerName");
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
    avatarLabel->setStyleSheet(R"(
        border-radius: 32px;
        border: 2px solid #e0e4ea;
        overflow: hidden;
    )");
    avatarLabel->setScaledContents(true);
    resize(900, 600);
    setMinimumSize(800, 500);
    summonerLabel->setText("");

    QVBoxLayout *rankLayout = new QVBoxLayout;
    queueSelector->addItems({"soloq", "flex"});
    connect(queueSelector, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx == 0)
            obtainInfo("RANKED_SOLO_5x5");
        else
            obtainInfo("RANKED_FLEX_SR");
    });

    auto *historyHeader = new QLabel("Match history");
    historyHeader->setObjectName("header");
    matchHistoryList->setSpacing(6);
    matchHistoryList->setStyleSheet(R"(
        QListWidget {
            background: #2c2c2c;
            border: 1px solid #3c3c3c;
            border-radius: 8px;
            font-size: 14px;
            padding: 8px;
            color: #dddddd;
        }
        QListWidget::item {
            margin: 8px 0;
            padding: 8px;
            border-radius: 6px;
            border: 1px solid #444;
        }
        QListWidget::item:selected {
            background: #444;
            color: #fff;
        }
    )");
    matchHistoryList->setWordWrap(true);

    rankInfoLabel->setText("");
    rankLayout->addWidget(queueSelector);
    rankLayout->addWidget(rankInfoLabel);

    topLayout->addWidget(avatarLabel);
    topLayout->addWidget(summonerLabel);
    topLayout->addLayout(rankLayout);

    waitingLayout->addLayout(topLayout);

    // separator
    QFrame *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setStyleSheet("color: #e0e4ea; margin: 8px 0;");
    waitingLayout->addWidget(line);

    waitingLayout->addWidget(historyHeader);
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
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(18);
    waitingLayout->setSpacing(16);
    topLayout->setSpacing(18);
    rankLayout->setSpacing(8);

    networkManager = new QNetworkAccessManager(this);

    createTrayIcon();

    gameStateTimer = new QTimer(this);
    connect(gameStateTimer, &QTimer::timeout, this, &MatchWindow::checkGameState);
    gameStateTimer->start(2000);

    switchToState(WAITING);
    loadChamps();
    loadQueues();
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
        toggleOverlayAction->setText("Turn off overlay");
        if (currentState == IN_GAME) {
            setupOverlay();
        }
    } else {
        toggleOverlayAction->setText("Turn on overlay");
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
                    QPixmap rawPix;
                    rawPix.loadFromData(avatarReply->readAll());
                    QPixmap circle(64, 64);
                    circle.fill(Qt::transparent);
                    QPainter painter(&circle);
                    painter.setRenderHint(QPainter::Antialiasing, true);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    QPainterPath path;
                    path.addEllipse(0, 0, 64, 64);
                    painter.setClipPath(path);
                    painter.drawPixmap(0, 0, 64, 64, rawPix.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    avatarLabel->setPixmap(circle);
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
                    double wr = (wins + losses > 0)
                        ? (double(wins) / double(wins + losses) * 100.0)
                        : 0.0;
                    rankInfoLabel->setText(QString("%1 %2 (%3W/%4L) ~%5% WR")
                        .arg(tier).arg(division).arg(wins).arg(losses).arg(wr, 0, 'f', 1));
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
            QJsonObject gamesObj = obj["games"].toObject();
            QJsonArray games = gamesObj["games"].toArray();
            matchHistoryList->clear();
            for (const QJsonValue &gameVal : games) {
                QJsonObject game = gameVal.toObject();
                QJsonArray participants = game["participants"].toArray();
                QJsonObject player;
                for (const QJsonValue &p : participants) {
                    QJsonObject part = p.toObject();
                    if (part["stats"].isObject()) {
                        player = part;
                        break;
                    }
                }
                int champId = player["championId"].toInt();
                QString champ = champIdName.value(champId, QString::number(champId));
                QJsonObject stats = player["stats"].toObject();
                int kills = stats["kills"].toInt();
                int deaths = stats["deaths"].toInt();
                int assists = stats["assists"].toInt();
                bool win = stats["win"].toBool();
                int queueId = game["queueId"].toInt();
                QString qName = qIdName.value(queueId, QString::number(queueId));
                double cs = stats["totalMinionsKilled"].toDouble()
                            + stats["neutralMinionsKilled"].toDouble();
                double gameDuration = game["gameDuration"].toDouble();
                double minutes = gameDuration > 0 ? (gameDuration / 60.0) : 1.0;
                double csPerMin = cs / minutes;

                matchHistoryList->addItem(QString("%1 (%2) %3/%4/%5 | %6 | CS: %7 (%8/min)")
                    .arg(champ)
                    .arg(win ? "Win" : "Loss")
                    .arg(kills)
                    .arg(deaths)
                    .arg(assists)
                    .arg(qName)
                    .arg(cs, 0, 'f', 0)
                    .arg(csPerMin, 0, 'f', 1));
            }
        }
        historyReply->deleteLater();
        qDebug() << "historia: "<< matchHistoryList->count();
    });
}

void MatchWindow::loadChamps(){
    QNetworkRequest req(QUrl("https://ddragon.leagueoflegends.com/cdn/14.12.1/data/en_US/champion.json"));
    QNetworkReply *reply = networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object()["data"].toObject();
            for (const QString &key : data.keys()) {
                QJsonObject champ = data[key].toObject();
                int id = champ["key"].toString().toInt();
                QString name = champ["name"].toString();
                champIdName[id] = name;
            }
        }
        reply->deleteLater();
    });
}

void MatchWindow::loadQueues(){
    qIdName[400] = "Normal Draft";
    qIdName[420] = "Ranked Solo";
    qIdName[430] = "Normal Blind";
    qIdName[440] = "Ranked Flex";
    qIdName[450] = "ARAM";
    qIdName[700] = "Clash";
    qIdName[900] = "URF";
    qIdName[1020] = "One for All";
}

