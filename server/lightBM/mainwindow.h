#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "QFile"
#include "QMouseEvent"
#include "QNetworkInterface"
#include "QTextStream"
#include "dialog.h"
#include "myclient.h"
#include "myriddle.h"
#include "myserver.h"
#include <QAction>
#include <QByteArray>
#include <QCheckBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QMovie>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStandardItem>
#include <QStyle>
#include <QTableWidgetItem>
#include <QVector>

namespace Ui {
class MainWindow;
}
class MyServer;
class MainWindow : public QMainWindow {
  Q_OBJECT
  Q_ENUMS(Cases) // metadata declaration
public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();
  Ui::MainWindow *ui;
  MyServer *server;
  MyRiddle *riddle;
  enum Cases { msg_alive, msg_main, msg_tcp }; // msg_def };
  bool consolLevelOutput[3] = {true, true, true};
  int freqKeepAliveReq;
  int numKeepAliveReq;
  QString cbHostAddress;
  int KeepAliveEnable;

private:
  QElapsedTimer *ETimer_UpdeteScenario;
  QTimer *timer_UpdeteScenario;
  int currentScenarioNum = 0;
  int nextScenarioNum = 0;
  QVector<int> *secretScenarios;
  QProcess *tftp;
  QProcess *ssh;
  Dialog *PopUpWindow;
  QSize gifIcn = QSize(70, 70);
  // QSize *gifIcn = new QSize(85, 85);
  int maxScenarioNum = 12;
  void on_actionQuit_triggered();
  QAction *consolKeepAliveVisibale;
  QAction *consolMainVisibale;
  QAction *consolNetworkVisibale;
  QPushButton *pbStartStopTcpServer;
  QPushButton *pbStartLogicTcpServer;
  QPushButton *pbPlayNextScenario;
  QPushButton *pbStartStopTcpClients;
  QPushButton *pbSetAndConvertScenario;

  QTabWidget *mainTabWidget;
  QPoint mpos;
  QMovie *mTraficLight;
  QMovie *mNetwork;
  QMovie *mSettings;
  QMovie *mStart;
  QPushButton *hideScenario;
  QPushButton *hideConsole;
  QSlider *pointSlider;
  QString separator = ",";
  QMessageBox *sorry;
  QStringList *oppsListAll;
  QStringList *oppsList;
  struct MyTraficLightStruct {
    // bool operator<(const MyTraficLightStruct &other) const { return pointNumber < other.pointNumber; }
    bool operator<(const MyTraficLightStruct &other) const { return scenarioName < other.scenarioName; }
    // int pointNumber;
    QString scenarioName;
    QString awaitTime;
    QString traficLight[4][2][5];
    int UniconLight = 0;
    int SecretScenario = 0;
  };
  QList<MyTraficLightStruct> traficLightStructList;
  struct MyTempScenario {
    QString scenarioName = "";
    QString awaitTime;
    QString traficLight[4];
    int UniconLight = 0;
    int SecretScenario = 0;
  };
  QSettings *settingsIni;
  QGridLayout *layoutTL[5];
  QPushButton *button[4][2][5];
  QPushButton *UniconLightButton[4];
  struct myTLightSettings {
    int gifRows = 32;
    int gifRols = 32;
    int chain_length = 5;
    int parallel = 2;
    int scan_mode = 0;
    int multiplexing = 2;
    QString led_rgb_sequence = "BGR";
    int pwm_bits = 7;
    int brightness = 70;
    int gpio_slowdown = 2;
    int delay = 125;
    int rotate = 0;
  };
  myTLightSettings TLightSettings[4];
  //  QList<myTLightSettings> TLightSettings;

private slots:
  void on_UniconSateChanged(int state);
  void on_SecretSateChanged(int state);
  void timerUpdeteScenario_Update();
  void TLsentAllScenarioS();
  void TLchangeState(int st);
  void TLsetNextScenario(bool secret, int st);
  void oopSmessage();
  void mainSetup();
  void saveAsCSV(QString filename);
  void readFromCSV(QString filename);
  void tabTraficLightsSetup();
  void setAnimationToMatrix(QPushButton *ptr, int k, int i, int j);
  void tabWidgetSetup();
  void tabNetInfoSetup();
  void tabAppConfigSetup();
  void setConsol();
  void loadIni();
  void setupUI();
  void mousePressEvent(QMouseEvent *event);
  void mouseMoveEvent(QMouseEvent *event);
  void fillNetworkDevicesTables();
  void on_actionSave_and_quit_triggered();
  void on_pbAddEvent_clicked();
  void on_leNewTimeEvent_textEdited(const QString &arg1);
  void on_tableWidgetScenario_itemChanged(QTableWidgetItem *item);
  void on_pushButton_clicked();
  void on_pushButton_2_clicked();
  void on_freqKeepAliveReq_valueChanged(int arg1);
  void on_numKeepAliveReq_valueChanged(int arg1);
  void on_pushButton_3_clicked();
  void on_comboBoxTCPHostIp_currentIndexChanged(const QString &arg1);
  void on_checkBox_stateChanged(int arg1);
  void on_lineEdit_2_returnPressed();
  void on_pushButton_4_clicked();
  void on_pushButton_5_clicked();
  void on_pushButton_6_clicked();
  void on_actionSave_triggered();
  void on_pushButton_7_clicked();
  void on_comboBox_currentIndexChanged(const QString &arg1);

public slots:
  void onTLbtnPresed(QString area, QString name, int val);
  void send_by_name(QString cur_erea, QString cur_name, QByteArray cmd);
  void printToConsol(QString data, int level);
  void on_UpdateDevicesList(QList<QTcpSocket *> &);
  void _newParsedMsg(QTcpSocket *, QByteArray);
signals:
  void SendCmdClient(const qintptr &, QString);
  void SendCmdClient(const qintptr &, QByteArray);
};

#endif // MAINWINDOW_H
