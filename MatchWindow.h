#ifndef MATCHWINDOW_H
#define MATCHWINDOW_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QWindow>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QAuthenticator>
#include <QFile>
#include <QRegularExpression>
#include <QComboBox>
#include <QListWidget>
#include <QJsonArray>

class MatchWindow : public QWidget {
    enum GameState {
        WAITING,
        CHAMP_SELECT,
        IN_GAME
    };

    Q_OBJECT
    GameState last;

public:
    explicit MatchWindow(QWidget *parent = nullptr);
    ~MatchWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

    private slots:
        void iconActivated(QSystemTrayIcon::ActivationReason reason);
    void toggleOverlay();
    void showNormal();
    void checkGameState();

private:
    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    QAction *showAction;
    QAction *toggleOverlayAction;
    QAction *quitAction;

    QStackedWidget *stackedWidget;
    QWidget *waitingWidget;
    QWidget *champSelectWidget;
    QWidget *inGameWidget;

    QTimer *gameStateTimer;
    QNetworkAccessManager *networkManager;
    QComboBox *queueSelector;

    QString riotClientPort;
    QString riotClientToken;
    QString summonersName;
    QString rank;

    QLabel *avatarLabel;
    QLabel *summonerLabel;
    QLabel *rankInfoLabel;
    QListWidget *matchHistoryList;

    GameState currentState;
    bool overlayEnabled;

    void createTrayIcon();
    void switchToState(GameState state);
    void setupOverlay();
    bool findRiotClientCredentials();
    void obtainInfo(QString qType);
};

#endif // MATCHWINDOW_H