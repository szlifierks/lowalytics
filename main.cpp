#include <QApplication>
#include "MatchWindow.h"


int main(int argc, char *argv[]) {
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    QSslConfiguration::setDefaultConfiguration(config);

    qputenv("QT_LOGGING_RULES", "qt.network.ssl.warning=false");

    QApplication app(argc, argv);

    MatchWindow window;
    window.setWindowTitle("");
    window.show();

    return app.exec();
}
