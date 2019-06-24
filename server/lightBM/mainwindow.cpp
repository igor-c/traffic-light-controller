#include "mainwindow.h"
#include "ui_mainwindow.h"
//#include <Python.h>
#include <QMessageBox>
#include <QPixmap>

#include <QSpinBox>
#include <QTableWidgetItem>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);
  server = new MyServer();
  riddle = new MyRiddle();

  QString fileName(QCoreApplication::applicationDirPath() + "/scenario.scv");
  QFile file(fileName);
  if (!QFileInfo::exists(fileName)) {
    file.open(QIODevice::ReadWrite | QIODevice::Text);
    file.close();
  }
  loadIni();
  setupUI();

  //  pointSlider = new QSlider(Qt::Horizontal, this);
  //  pointSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  //  // ui->gridLayout_5->addWidget(pointSlider, 0, 0);
  //  pointSlider->setRange(0, 1);
  //  pointSlider->setSingleStep(1);
  readFromCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
  printToConsol(QString("Begin initialization"), msg_main);

  connect(this, SIGNAL(SendCmdClient(const qintptr &, QString)), server, SLOT(on_SendCmdClient(const qintptr &, QString)));
  connect(this, SIGNAL(SendCmdClient(const qintptr &, QByteArray)), server, SLOT(on_SendCmdClient(const qintptr &, QByteArray)));
  QObject::connect(riddle, &MyRiddle::new_autorization, server, &MyServer::on_new_autorization);
  QObject::connect(server, &MyServer::unbind_dev_ip, riddle, &MyRiddle::_unbind_dev_ip);

  timer_UpdeteScenario = new QTimer(this);
  connect(timer_UpdeteScenario, SIGNAL(timeout()), this, SLOT(timerUpdeteScenario_Update()));
  ETimer_UpdeteScenario = new QElapsedTimer();
  //  cbHostAddress = ui->comboBoxTCPHostIp->currentText();
  //  imageLabel = new QLabel;
  //  scrollArea = new QScrollArea;
  //  imageLabel->setBackgroundRole(QPalette::Base);
  //  imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  //  imageLabel->setScaledContents(true);

  //  scrollArea->setBackgroundRole(QPalette::Dark);
  //  scrollArea->setWidget(imageLabel);
  //  scrollArea->setVisible(false);*
}
MainWindow::~MainWindow() { delete ui; }
void MainWindow::timerUpdeteScenario_Update() {
  if (traficLightStructList.size() >= currentScenarioNum) {
    if (QDateTime::fromTime_t(ETimer_UpdeteScenario->elapsed() / 1000).toUTC().toString("hh:mm:ss") ==
        traficLightStructList[currentScenarioNum].awaitTime) {
      printToConsol(QString("Scenario time ended"), msg_main);
      emit pbPlayNextScenario->clicked();
    }
  }
}
void MainWindow::mousePressEvent(QMouseEvent *event) { mpos = event->pos(); }
void MainWindow::mouseMoveEvent(QMouseEvent *event) {
  if (event->buttons() & Qt::LeftButton) {
    QPoint diff = event->pos() - mpos;
    QPoint newpos = this->pos() + diff;
    this->move(newpos);
  }
}
void MainWindow::tabWidgetSetup() {
  ui->splitter->setStyleSheet("QSplitter::handle {background-color: rgb(80, 10, 10); border: 1px solid #777; width: 13px; margin-top: "
                              "2px;margin-bottom: 2px; border-radius: 4px;}");

  ui->tabWidget->setDocumentMode(true);
  ui->tabWidget->setCurrentIndex(0);
  ui->tabWidget->setStyleSheet("QTabWidget::tab::iconSize { height: 10px; width: 10px; }");
  ui->tabWidget->setIconSize(QSize(30, 30));

  mTraficLight = new QMovie(":/myMedia/traficLight1.gif");
  mNetwork = new QMovie(":/myMedia/network.gif");
  mSettings = new QMovie(":/myMedia/settings.gif");
  mStart = new QMovie(":/myMedia/start.gif");

  int speed = 30;
  mTraficLight->setSpeed(speed);
  mNetwork->setSpeed(speed);
  mSettings->setSpeed(speed);
  mStart->setSpeed(speed);

  QObject::connect(mTraficLight, &QMovie::frameChanged, this, [this]() { ui->tabWidget->setTabIcon(0, QIcon(mTraficLight->currentPixmap())); });
  QObject::connect(mNetwork, &QMovie::frameChanged, this, [this]() { ui->tabWidget->setTabIcon(1, QIcon(mNetwork->currentPixmap())); });
  QObject::connect(mSettings, &QMovie::frameChanged, this, [this]() { ui->tabWidget->setTabIcon(2, QIcon(mSettings->currentPixmap())); });
  mTraficLight->start();
  mNetwork->start();
  mSettings->start();
  mTraficLight->stop();
  mNetwork->stop();
  mSettings->stop();

  ui->tabWidget->setTabIcon(0, QIcon(mTraficLight->currentPixmap()));
  ui->tabWidget->setTabIcon(1, QIcon(mNetwork->currentPixmap()));
  ui->tabWidget->setTabIcon(2, QIcon(mSettings->currentPixmap()));

  QWidget *widget = new QWidget();
  QGridLayout *layout = new QGridLayout(widget);
  layout->setAlignment(Qt::AlignCenter);
  // layout->setAlignment(widget, Qt::AlignTop);
  layout->setMargin(0);
  layout->setSpacing(0);

  //  QHBoxLayout *layout = new QHBoxLayout(widget);

  pbStartStopTcpServer = new QPushButton(this);
  layout->setAlignment(pbStartStopTcpServer, Qt::AlignTop);
  pbStartStopTcpServer->setText("Start Server");
  pbStartStopTcpServer->setMaximumSize(QSize(100, 40));
  pbStartStopTcpServer->setMinimumSize(QSize(100, 40));

  // ui->tabWidget->setCornerWidget(pbStartStopTcpServer, Qt::TopRightCorner);
  QObject::connect(pbStartStopTcpServer, &QPushButton::clicked, this, [this]() {
    if (pbStartStopTcpServer->text() == "Stop Server") {
      timer_UpdeteScenario->stop();
      pbStartLogicTcpServer->setEnabled(false);
      pbPlayNextScenario->setEnabled(false);
      pbStartStopTcpServer->setText("Start Server");
      mTraficLight->setPaused(true);
      mNetwork->setPaused(true);
      mSettings->setPaused(true);
      mStart->setPaused(true);
      server->StopServer();
      printToConsol(QString("Stop server"), msg_main);
    } else {
      if (pbStartStopTcpClients->text() == "Stop Clients") {
        pbStartLogicTcpServer->setEnabled(true);
      }
      pbStartStopTcpClients->setEnabled(true);
      pbStartStopTcpServer->setText("Stop Server");
      if (ui->uiAnimation->isChecked()) {
        mTraficLight->start();
        mNetwork->start();
        mSettings->start();
        mStart->start();
      }
      server->StartServer(ui->lineEditTCPport->text().toInt());
      printToConsol(QString("Start Server"), msg_main);
    }
  });

  pbStartStopTcpClients = new QPushButton(this);
  layout->setAlignment(pbStartStopTcpClients, Qt::AlignTop);
  pbStartStopTcpClients->setText("Start Clients");
  pbStartStopTcpClients->setMaximumSize(QSize(100, 40));
  pbStartStopTcpClients->setMinimumSize(QSize(100, 40));

  // ui->tabWidget->setCornerWidget(pbStartStopTcpServer, Qt::TopRightCorner);
  QObject::connect(pbStartStopTcpClients, &QPushButton::clicked, this, [this]() {
    if (pbStartStopTcpClients->text() == "Stop Clients") {
      printToConsol(QString("Stop Clients"), msg_main);
      pbStartLogicTcpServer->setEnabled(false);
      pbPlayNextScenario->setEnabled(false);
      pbStartStopTcpClients->setText("Start Clients");
      ssh = new QProcess(this);
      QStringList optionList_ssh;
      ssh->setProcessChannelMode(QProcess::MergedChannels);
      QString cmd = ui->lePythonPath->text() + " " + QCoreApplication::applicationDirPath() + "/include/script/pySSH.py -s 0";
      ssh->waitForFinished(-1);
      ssh->start(cmd);
    } else {
      qDebug() << "do pySSH";

      if (pbStartStopTcpServer->text() == "Stop Server") {
        pbStartLogicTcpServer->setEnabled(true);
      }
      pbStartStopTcpClients->setText("Stop Clients");
      if (ui->uiAnimation->isChecked()) {
        mTraficLight->start();
        mNetwork->start();
        mSettings->start();
        mStart->start();
      }
      ssh = new QProcess(this);
      QStringList optionList_ssh;
      ssh->setProcessChannelMode(QProcess::MergedChannels);
      QString cmd = ui->lePythonPath->text() + " " + QCoreApplication::applicationDirPath() + "/include/script/pySSH.py -s 1" + " -m " +
                    QString::number(TLightSettings[0].multiplexing) + " -m " + QString::number(TLightSettings[1].multiplexing) + " -m " +
                    QString::number(TLightSettings[2].multiplexing) + " -m " + QString::number(TLightSettings[3].multiplexing);
      qDebug() << cmd;
      ssh->waitForFinished(-1);
      ssh->start(cmd);
      //      ssh = new QProcess;
      //      QStringList optionList_ssh;
      //      QStringList clientList;
      //      ssh->setProcessChannelMode(QProcess::MergedChannels);
      //      optionList_ssh << QCoreApplication::applicationDirPath() + "/include/script/pySSH.py";
      //      //      optionList_tftp << "-i" << "192.168.88.101";
      //      //      optionList_tftp << "sudo killall client";
      //      //      optionList_tftp << "sudo ./client";
      //      qDebug() << optionList_ssh;
      //      ssh->waitForFinished(-1);
      //      ssh->start("python", optionList_ssh);
      printToConsol(QString("Start Clients"), msg_main);
    }
  });
  pbStartStopTcpClients->setEnabled(false);

  pbSetAndConvertScenario = new QPushButton(this);
  layout->setAlignment(pbSetAndConvertScenario, Qt::AlignTop);
  pbSetAndConvertScenario->setText("Upload Scen..");
  pbSetAndConvertScenario->setMaximumSize(QSize(150, 40));
  pbSetAndConvertScenario->setMinimumSize(QSize(150, 40));
  QObject::connect(pbSetAndConvertScenario, &QPushButton::clicked, this, [this]() { TLsentAllScenarioS(); });

  pbStartLogicTcpServer = new QPushButton(this);
  layout->setAlignment(pbStartLogicTcpServer, Qt::AlignTop);
  pbStartLogicTcpServer->setText("Play Logic");
  pbStartLogicTcpServer->setMaximumSize(QSize(100, 40));
  pbStartLogicTcpServer->setMinimumSize(QSize(100, 40));

  // ui->tabWidget->setCornerWidget(pbStartLogicTcpServer, Qt::TopRightCorner);
  QObject::connect(pbStartLogicTcpServer, &QPushButton::clicked, this, [this]() {
    if (pbStartLogicTcpServer->text() == "Stop Logic") {
      pbPlayNextScenario->setEnabled(false);
      ui->tableWidgetScenario->setEnabled(true);
      TLchangeState(0);
      pbStartLogicTcpServer->setText("Play Logic");
      printToConsol(QString("Stop logic"), msg_main);
    } else {
      ui->tableWidgetScenario->setEnabled(false);
      pbPlayNextScenario->setEnabled(true);
      //      TLsentAllScenarioS();
      TLchangeState(1);
      pbStartLogicTcpServer->setText("Stop Logic");
      if (ui->uiAnimation->isChecked()) {
      }
      printToConsol(QString("Start logic"), msg_main);
    }
  });
  pbStartLogicTcpServer->setEnabled(false);

  pbPlayNextScenario = new QPushButton(this);
  layout->setAlignment(pbPlayNextScenario, Qt::AlignTop);
  pbPlayNextScenario->setText("Play Next");
  secretScenarios = new QVector<int>;
  pbPlayNextScenario->setMaximumSize(QSize(100, 40));
  pbPlayNextScenario->setMinimumSize(QSize(100, 40));
  QObject::connect(pbPlayNextScenario, &QPushButton::clicked, this, [this]() { TLsetNextScenario(false, -1); });
  pbPlayNextScenario->setEnabled(false);

  //  pbStartStopTcpServer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  //  pbStartLogicTcpServer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  //  pbPlayNextScenario->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

  //  pbStartStopTcpServer->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
  //  pbStartLogicTcpServer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  layout->addWidget(pbStartStopTcpServer, 0, 0, Qt::AlignVCenter | Qt::AlignLeft);
  layout->addWidget(pbStartStopTcpClients, 0, 1, Qt::AlignVCenter | Qt::AlignLeft);
  layout->addWidget(pbSetAndConvertScenario, 0, 2, Qt::AlignVCenter | Qt::AlignLeft);
  layout->addWidget(pbStartLogicTcpServer, 0, 3, Qt::AlignVCenter | Qt::AlignLeft);
  layout->addWidget(pbPlayNextScenario, 0, 4, Qt::AlignVCenter | Qt::AlignLeft);
  //  layout->addWidget(pbStartStopTcpServer);
  //  layout->addWidget(pbStartLogicTcpServer);

  ui->tabWidget->setCornerWidget(widget, Qt::TopRightCorner);
}
void MainWindow::TLsentAllScenarioS() {
  qDebug() << "traficLightStructList: " << traficLightStructList.size();
  for (int k = 0; k < 4; k++) {
    for (int r = 0; r < traficLightStructList.size(); r++) {
      uint filename[16];
      //      enum class Type : uint8_t { COMMUNICATION, STATE, SETTINGS, DATA };
      //      enum class Communication : uint8_t { WHO, ACK, RCV };
      //      enum class State : uint8_t { STOP, START, RESET };
      //      enum class DATA : uint8_t { UNICON, BUTTON, SCENARIO};
      filename[0] = 0x0;                  // sizeH
      filename[1] = 0xE;                  // sizeL
      filename[2] = 3;                    // Type-DATA
      filename[3] = 2;                    // DATA-SCENARIO
      filename[4] = 0;                    // NEWSCENARIO
      filename[5] = static_cast<uint>(r); // SCENARIO №

      // gif file names
      for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 5; j++) {
          QString strName = traficLightStructList[r].traficLight[k][i][j];
          quint16 intName = strName.toUInt();
          quint8 uintNameH = quint8((intName >> 8) & 0xff);
          quint8 uintNameL = quint8(intName & 0xff);
          filename[i * 5 + j + 6] = uintNameH;
          filename[i * 5 + j + 6] = uintNameL;
        }
      }

      QByteArray arr; // = QByteArray::fromRawData(filename, sizeof(filename));
      for (int r = 0; r < 16; r++) {
        arr.append(filename[r]);
      }
      QString name;
      if (k == 0)
        name = "1/2";
      else if (k == 1)
        name = "3/4";
      else if (k == 2)
        name = "5/6";
      else if (k == 3)
        name = "7/8";
      send_by_name("trafic light", name, arr);
    }
  }
}
void MainWindow::onTLbtnPresed(QString area, QString name, int val) {
  //  if (val == 1) {
  //    emit pbPlayNextScenario->clicked(); // TLsetNextScenario(false, -1);
  //  } else if (val == 1) {
  //    TLsetNextScenario(true, -1);
  //  }
  if (val == 1) {
    TLsetNextScenario(false, -1);
  } else if (val == 2) {
    TLsetNextScenario(true, -1);
  }
}
void MainWindow::TLsetNextScenario(bool secret, int SetByVal = -1) {
  bool fDo = true;
  int cnt0 = 0;
  bool sent = true;
  while (fDo) {
    if (SetByVal == -1) {
      currentScenarioNum = nextScenarioNum;
      nextScenarioNum++;
      if (nextScenarioNum >= traficLightStructList.size()) {
        nextScenarioNum = 0;
      }
    } else {
      currentScenarioNum = SetByVal;
    }
    fDo = false;

    if (traficLightStructList.size() >= currentScenarioNum) {
      if (secret) {
        fDo = traficLightStructList[currentScenarioNum].SecretScenario > 0 ? false : true;
      } else {
        fDo = traficLightStructList[currentScenarioNum].SecretScenario == 0 ? false : true;
      }
    }
    cnt0++;
    if (cnt0 > traficLightStructList.size()) {
      fDo = false;
      sent = false;
    }

    //    if (!secret) {
    //      if (traficLightStructList.size() >= currentScenarioNum) {
    //        fDo = traficLightStructList[currentScenarioNum].SecretScenario == 2 ? true : false;
    //      }
    //    } else {
    //      if (traficLightStructList.size() >= currentScenarioNum) {
    //        fDo = traficLightStructList[currentScenarioNum].SecretScenario == 0 ? true : false;
    //      }
    //    }

    //    fDo = false;
    //    if (!secret) {
    //      for (int var = 0; var < secretScenarios->size(); ++var) {
    //        if (currentScenarioNum == secretScenarios->at(var)) {
    //          fDo = true;
    //        }
    //      }
    //    } else {
    //    }
  }

  if (sent) {
    qDebug() << "currentScenarioNum: " << currentScenarioNum;
    char mydata1[] = {'\x00', '\x04', '\x03', '\x02', '\x01', static_cast<char>(currentScenarioNum)};
    int cnt = sizeof riddle->SubGroupNamelist / sizeof riddle->SubGroupNamelist[0];
    for (int var = 0; var < cnt; ++var) {
      send_by_name("trafic light", riddle->SubGroupNamelist[var], QByteArray::fromRawData(mydata1, sizeof(mydata1)));
      // 4.1 UNICON () led
      //      char mydata2[] = {'\x00', '\x03', '\x03', '\x00', static_cast<char>(traficLightStructList[currentScenarioNum].UniconLight)};
      //      send_by_name("trafic light", riddle->SubGroupNamelist[var], QByteArray::fromRawData(mydata2, sizeof(mydata2)));
    }
    ETimer_UpdeteScenario->restart();
    printToConsol(QString("Play new scenario: " + QString::number(currentScenarioNum)), msg_main);
    qDebug() << "--\n";
  }
}
void MainWindow::TLchangeState(int st) {
  char mydata[] = {'\x00', '\x02', '\x01', static_cast<char>(st)};
  int cnt = sizeof riddle->SubGroupNamelist / sizeof riddle->SubGroupNamelist[0];
  for (int var = 0; var < cnt; var++) {
    send_by_name("trafic light", riddle->SubGroupNamelist[var], QByteArray::fromRawData(mydata, sizeof(mydata)));
  }
  if (st) {
    TLsetNextScenario(false, -1);
    timer_UpdeteScenario->start(1000);
    ETimer_UpdeteScenario->restart();
  } else {
    timer_UpdeteScenario->stop();
    nextScenarioNum = 0;
  }
}
void MainWindow::tabTraficLightsSetup() {
  ui->splitter_2->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

  for (int k = 0; k < 4; ++k) {
    layoutTL[k] = new QGridLayout;
    layoutTL[k]->setVerticalSpacing(0);
    layoutTL[k]->setHorizontalSpacing(10);
    // layoutTL[k]->setSpacing(0);
    layoutTL[k]->setMargin(0);
    //    layoutTL[k]->setSizeConstraint(QLayout::SetFixedSize);

    QLabel *TLname = new QLabel("Trafic Light №");
    QString str = QString::number((k + 1) * 2 - 1) + "/" + QString::number((k + 1) * 2);
    TLname->setAlignment(Qt::AlignCenter);
    TLname->setMargin(0);
    TLname->setStyleSheet("QLabel { background-color : rgb(179, 0, 0)}");
    TLname->setText(TLname->text().append(str));
    TLname->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    layoutTL[k]->addWidget(TLname, 0, 0, 1, 2);

    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 5; ++j) {
        auto text = QStringLiteral("(%1,%2,%3)").arg(k).arg(i).arg(j);
        button[k][i][j] = new QPushButton();
        button[k][i][j]->setProperty("TLnum", int(k));
        button[k][i][j]->setProperty("TLsubnum", int(i));
        button[k][i][j]->setProperty("matrixNum", int(j));
        QPushButton *ptr;
        ptr = button[k][i][j];
        button[k][i][j]->setStyleSheet("QPushButton { background-color: black }");
        button[k][i][j]->setFixedSize(gifIcn);
        // button[k][i][j]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        if (j < 3)
          layoutTL[k]->addWidget(button[k][i][j], j + 1, i);
        else if (j == 3) {
          //          QSpacerItem *vSpacer = new QSpacerItem(0, 5, QSizePolicy::Ignored, QSizePolicy::Ignored);
          //          layoutTL[k]->addItem(vSpacer, j + 1, 0, 1, 2);
          QLabel *lb = new QLabel();
          lb->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
          layoutTL[k]->addWidget(lb, j + 1, 0, 1, 2);
          layoutTL[k]->addWidget(button[k][i][j], j + 2, i);
        } else
          layoutTL[k]->addWidget(button[k][i][j], j + 2, i);

        QObject::connect(button[k][i][j], &QPushButton::clicked, [this, ptr, text, k, i, j] { setAnimationToMatrix(ptr, k, i, j); });
      }
    }

    //    UniconLightButton[k] = new QPushButton(this);
    //    UniconLightButton[k]->setText("UniconLight ON");
    //    layoutTL[k]->addWidget(UniconLightButton[k], 7, 0, 1, 2);
    //    QObject::connect(UniconLightButton[k], &QPushButton::clicked(), [this, k] {
    //      if (UniconLightButton[k]->text() == "UniconLight ON") {
    //        UniconLightButton[k]->setText("UniconLight Off");
    //        static const char mydata[] = {'\x00', '\x03', '\x03', '\x00', '\x01'};
    //        send_by_name("trafic light", "1/2", QByteArray::fromRawData(mydata, sizeof(mydata)));

    //      } else {
    //        static const char mydata[] = {'\x00', '\x03', '\x03', '\x00', '\x00'};
    //        send_by_name("trafic light", "1/2", QByteArray::fromRawData(mydata, sizeof(mydata)));
    //        UniconLightButton[k]->setText("UniconLight ON");
    //      }
    //    });

    ui->hLayout->addLayout(layoutTL[k]);
    if (k < 3)
      ui->hLayout->addSpacing(20);
  }

  //  for (int k = 0; k < 4; ++k) {
  ////    QSpacerItem *vSpacer = new QSpacerItem(400, 400, QSizePolicy::Ignored, QSizePolicy::Ignored);
  ////    layoutTL[k]->addItem(vSpacer, 0, 2);
  //    //    QLabel *lb = new QLabel("-------------");
  //    //    layoutTL[k]->addWidget(lb, 3, 0);
  //  }

  ui->pbAddEvent->setEnabled(false);

  QTableWidget *table;
  table = ui->tableWidgetScenario;
  //  table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  //  table->setAutoScroll(true);
  table->setEnabled(true);
  //    table->setSelectionMode(QAbstractItemView::NoSelection);
  //    table->setEditTriggers( QAbstractItemView::NoEditTriggers);

  QStringList tableColNames;
  //<< "№"

  tableColNames << "Name"
                << "AwaitTime"
                << "TR1/2"
                << "TR3/4"
                << "TR5/6"
                << "TR7/8"
                << "UniconLight"
                << "SecretScenario";
  table->setColumnCount(tableColNames.size());
  table->setRowCount(0);

  for (int var = 0; var < tableColNames.size(); ++var) {
    table->setHorizontalHeaderItem(var, new QTableWidgetItem(tableColNames.at(var)));
  }
  table->resizeColumnsToContents();
  table->setContextMenuPolicy(Qt::CustomContextMenu);

  QObject::connect(table, &QTableWidget::customContextMenuRequested, this, [this]() {
    if (ui->tableWidgetScenario->currentRow() < 0) {
      return;
    }
    QMenu *menu = new QMenu;
    QString name = ui->tableWidgetScenario->item(ui->tableWidgetScenario->currentRow(), 0)->text();
    if (name.size() == 0)
      name = "(name field is empty)";
    else
      name.append("\"").prepend("\"");

    QAction *del = new QAction(tr("Delete current scenario"), this);
    del->setStatusTip("Scenario with name: " + name + " will be deleted from this table");

    QAction *Show = new QAction(tr("Show current scenario"), this);
    Show->setStatusTip("Scenario with name: " + name + " will be loaded to trafic light shematic");

    QAction *Up = new QAction(tr("Move up"), this);
    Up->setStatusTip("Scenario with name: " + name + " will be moved UP at this table");

    QAction *Down = new QAction(tr("Move down"), this);
    Down->setStatusTip("Scenario with name: " + name + " will be moved DOWN at this table");

    QAction *Play = new QAction(tr("Play current scenario"), this);
    Play->setStatusTip("Scenario with name: " + name + " will be played");

    //-------------1-------------
    menu->addAction(del);
    menu->QObject::connect(del, &QAction::triggered, this, [this, menu]() {
      traficLightStructList.removeAt(ui->tableWidgetScenario->currentRow());
      //          //ui->tableWidgetTarget->removeRow(ui->tableWidgetTarget->currentRow());
      saveAsCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
      readFromCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
      //      a->deleteLater();
      menu->deleteLater();
    });
    //-------------2-------------
    menu->addAction(Show);
    QObject::connect(Show, &QAction::triggered, this, [this, menu]() {
      for (int k = 0; k < 4; ++k) {
        for (int i = 0; i < 2; ++i) {
          for (int j = 0; j < 5; ++j) {
            QString fileName = traficLightStructList[ui->tableWidgetScenario->currentRow()].traficLight[k][i][j];
            QString filePath;

            if (QDir(ui->leDefaultMediaFolderPath->text()).exists()) {
              filePath = ui->leDefaultMediaFolderPath->text() + "/" + fileName + ".gif";
            } else {
              return;
            }
            qDebug() << QStringLiteral("%1,%2,%3 %4").arg(k).arg(i).arg(j).arg(filePath);

            QPushButton *ptr = button[k][i][j];
            if (!filePath.isNull()) {
              ptr->setProperty("gifFileName", fileName);
              QPixmap pmap(filePath);
              QSize size(ptr->size().width(), ptr->size().height());
              pmap = pmap.scaled(size);
              ptr->setIconSize(size);
              ptr->setIcon(pmap);
            }
          }
        }
      }
      //      a->deleteLater();
      menu->deleteLater();
    });
    //-------------3-------------
    menu->addAction(Up);
    QObject::connect(Up, &QAction::triggered, this, [this, menu]() {
      QTableWidget *ptr;
      ptr = ui->tableWidgetScenario;
      int crow = ptr->currentRow();
      if (crow == 0) {
        return;
      }
      traficLightStructList.move(crow, crow - 1);
      saveAsCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
      readFromCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
      //      a->deleteLater();
      menu->deleteLater();
    });
    //-------------4-------------
    menu->addAction(Down);
    QObject::connect(Down, &QAction::triggered, this, [this, menu]() {
      QTableWidget *ptr;
      ptr = ui->tableWidgetScenario;
      int crow = ptr->currentRow();
      if (crow == ptr->rowCount() - 1) {
        return;
      }
      traficLightStructList.move(crow, crow + 1);
      saveAsCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
      readFromCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
      //      a->deleteLater();
      menu->deleteLater();
    });
    //-------------5-------------
    //    menu->addAction(Play);
    //    QObject::connect(Play, &QAction::triggered, this, [this, menu]() {
    //      QTableWidget *ptr;
    //      ptr = ui->tableWidgetScenario;
    //      int crow = ptr->currentRow();
    //      if (crow == ptr->rowCount() - 1) {
    //        return;
    //      }
    //      menu->deleteLater();
    //    });

    //-------------6-------------
    //    menu->addAction("start selected target");
    //    QObject::connect(&QAction::triggered, this, [this, menu]() {
    //      QTableWidget *table;
    //      table = ui->tableWidgetScenario;
    //      QString str = "";
    //      for (int i = 0; i < table->rowCount(); i++) {
    //        QWidget *pWidget = table->cellWidget(i, 5);
    //        QCheckBox *checkbox = pWidget->findChild<QCheckBox *>();
    //        if (checkbox) {
    //          if (checkbox->isChecked()) {
    //            str += "*" + table->item(i, 1)->text();
    //            str += "*" + table->item(i, 2)->text();
    //            str += "*" + table->item(i, 3)->text();
    //          }
    //        }
    //      }
    //      menu->deleteLater();
    //    });
    menu->exec(QCursor::pos());
    menu->clear();
  });
}
void MainWindow::saveAsCSV(QString filename) {
  QFile f(filename);
  QFileInfo fileInfo(filename);

  if (f.open(QFile::WriteOnly | QFile::Truncate)) {
    QTextStream data(&f);
    QStringList strList;
    if (fileInfo.baseName() == "scenario") {
      // qSort(targetStructList);
      for (int r = 0; r < traficLightStructList.size(); ++r) {
        strList.clear();
        // strList << "\"" + QString::number(traficLightStructList[r].pointNumber) + "\"";
        strList << "\"" + traficLightStructList[r].scenarioName + "\"";
        strList << "\"" + traficLightStructList[r].awaitTime + "\"";

        for (int k = 0; k < 4; ++k) {
          QString str;
          for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 5; ++j) {
              str.append(traficLightStructList[r].traficLight[k][i][j] + separator);
            }
          }
          strList << "\"" + str + "\"";
        }
        strList << "\"" + QString::number(traficLightStructList[r].UniconLight) + "\"";
        strList << "\"" + QString::number(traficLightStructList[r].SecretScenario) + "\"";
        //        strList << "\"" + traficLightStructList[r].traficLight12 + "\"";
        //        strList << "\"" + traficLightStructList[r].traficLight34 + "\"";
        //        strList << "\"" + traficLightStructList[r].traficLight56 + "\"";
        //        strList << "\"" + traficLightStructList[r].traficLight78 + "\"";
        data << strList.join(";") + "\n";
      }
    }
    f.close();
  }
  printToConsol(QString("save table data to scv file"), msg_main);
}
void MainWindow::readFromCSV(QString filename) {
  // pointSlider->setMaximum(traficLightStructList.size());

  QFile file(filename);
  QFileInfo fileInfo(filename);

  QTableWidget *table;
  table = ui->tableWidgetScenario;
  if (fileInfo.baseName() == "scenario") {
    MyTraficLightStruct traficLightStruct;
    if (file.open(QIODevice::ReadOnly)) {
      printToConsol(QString("read table data from scv file"), msg_main);
      int lineindex = 0;
      QTextStream in(&file);
      traficLightStructList.clear();

      while (!in.atEnd()) {
        QString fileLine = in.readLine();
        QStringList lineToken = fileLine.split(";", QString::SkipEmptyParts);
        traficLightStructList.append(traficLightStruct);
        for (int j = 0; j < lineToken.size(); j++) {
          QString value = lineToken.at(j);
          if (value.endsWith('"'))
            value.chop(1);
          if (value.startsWith('"'))
            value.remove(0, 1);
          if (j == 0)
            traficLightStructList[traficLightStructList.size() - 1].scenarioName = value;
          else if (j == 1)
            traficLightStructList[traficLightStructList.size() - 1].awaitTime = value;
          else if (j > 1 && j < 6) {
            QStringList strList = value.split(separator);
            for (int var = 0; var < strList.size() - 1; ++var) {
              traficLightStructList[traficLightStructList.size() - 1].traficLight[j - 2][var / 5][var - 5 * (int(var / 5))] = strList.at(var);
            }
          } else if (j == 6) {
            traficLightStructList[traficLightStructList.size() - 1].UniconLight = value.toInt();
          } else if (j == 7) {
            traficLightStructList[traficLightStructList.size() - 1].SecretScenario = value.toInt();
          }
        }
        lineindex++;
      }
    }
    file.close();

    table->blockSignals(true);
    table->setRowCount(0);
    for (int r = 0; r < traficLightStructList.size(); r++) {
      table->setRowCount(table->rowCount() + 1);
      table->setItem(r, 0, new QTableWidgetItem(QString(traficLightStructList[r].scenarioName)));
      table->setItem(r, 1, new QTableWidgetItem(QString(traficLightStructList[r].awaitTime)));

      for (int k = 0; k < 4; k++) {
        QString str = "";
        for (int i = 0; i < 2; i++) {
          for (int j = 0; j < 5; j++) {
            str.append(traficLightStructList[r].traficLight[k][i][j] + separator);
          }
        }
        // qDebug() << str;
        table->setItem(r, k + 2, new QTableWidgetItem(str));
      }

      // add QCheckBox in cell
      QWidget *wdg = new QWidget;         // first create a new widget
      QCheckBox *ccBox = new QCheckBox(); // lets say we want a checkbox in the cell
      ccBox->setProperty("raw", (int)r);
      ccBox->setCheckState(traficLightStructList[r].UniconLight == 2 ? Qt::Checked : Qt::Unchecked);
      QHBoxLayout *layout = new QHBoxLayout(wdg); // create the layout ON THE HEAP! (else after the function ends, layout is gone)
      layout->addWidget(ccBox);
      connect(ccBox, SIGNAL(stateChanged(int)), this, SLOT(on_UniconSateChanged(int))); // add the checkbox to the widget
      layout->setAlignment(Qt::AlignCenter); // center align the layout (box ends up in the center horizontally and vertically)
      layout->setMargin(0);
      wdg->setLayout(layout);          // set the layout on the widget, it takes ownership of the layout (don't need to delete it later)
      table->setCellWidget(r, 6, wdg); // targetStructList[i].cbStTarget);

      // add QCheckBox in cell
      QWidget *wdg2 = new QWidget;         // first create a new widget
      QCheckBox *ccBox2 = new QCheckBox(); // lets say we want a checkbox in the cell
      ccBox2->setProperty("raw", (int)r);
      ccBox2->setCheckState(traficLightStructList[r].SecretScenario == 2 ? Qt::Checked : Qt::Unchecked);
      connect(ccBox2, SIGNAL(stateChanged(int)), this, SLOT(on_SecretSateChanged(int)));
      QHBoxLayout *layout2 = new QHBoxLayout(wdg2); // create the layout ON THE HEAP! (else after the function ends, layout is gone)
      layout2->addWidget(ccBox2);                   // add the checkbox to the widget
      layout2->setAlignment(Qt::AlignCenter);       // center align the layout (box ends up in the center horizontally and vertically)
      layout2->setMargin(0);
      wdg2->setLayout(layout2);         // set the layout on the widget, it takes ownership of the layout (don't need to delete it later)
      table->setCellWidget(r, 7, wdg2); // targetStructList[i].cbStTarget);
    }
    //объединение одинаковых ячеек
    //    int fst = 0;
    //    int cnt = 1;
    //    for (int i = 1; i < table->rowCount(); i++) {
    //      if (table->item(i, 0)->text() == table->item(i - 1, 0)->text()) {
    //        table->setSpan(fst, 0, cnt + 1, 1);
    //        cnt++;
    //      } else {
    //        fst = i;
    //        cnt = 1;
    //      }
    //    }
    table->resizeColumnsToContents();
    table->blockSignals(false);
  }

  //  if (traficLightStructList.size() > 0) {
  //    ui->gridLayout_5->addWidget(pointSlider, 0, 0, 1, traficLightStructList.size());
  //  } else {
  //    ui->gridLayout_5->addWidget(pointSlider, 0, 0, 0, 1);
  //  }

  //  for (int i = 0; i < traficLightStructList.size(); i++) {
  //    QLabel *label1 = new QLabel(traficLightStructList[i].awaitTime);
  //    ui->gridLayout_5->addWidget(label1, 1, i, 1, 1);
  //  }
}
void MainWindow::on_UniconSateChanged(int state) {
  traficLightStructList[sender()->property("raw").toInt()].UniconLight = state;
  qDebug() << "Unicon: " << traficLightStructList[sender()->property("raw").toInt()].UniconLight << sender()->property("raw").toInt();
  saveAsCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
  readFromCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
}
void MainWindow::on_SecretSateChanged(int state) {
  traficLightStructList[sender()->property("raw").toInt()].SecretScenario = state;
  qDebug() << "Secret: " << traficLightStructList[sender()->property("raw").toInt()].SecretScenario << sender()->property("raw").toInt();
  saveAsCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
  readFromCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
}
void MainWindow::tabNetInfoSetup() {
  fillNetworkDevicesTables();
  ui->tableWidgetNetworkDevices->setFrameShape(QFrame::NoFrame);
}
void MainWindow::tabAppConfigSetup() {

  // cbHostAddress = ui->comboBoxTCPHostIp->currentText();

  ui->comboBoxTCPHostIp->addItem("Any");
  ui->comboBoxTCPHostIp->addItem("LocalHost");
  foreach (QNetworkInterface interface, QNetworkInterface::allInterfaces()) {
    if (interface.flags().testFlag(QNetworkInterface::IsUp) && !interface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
      foreach (QNetworkAddressEntry entry, interface.addressEntries()) {
        if (interface.hardwareAddress() != "00:00:00:00:00:00" && entry.ip().toString().contains(".")) {
          qDebug() << interface.name() + " " + entry.ip().toString() + " " + interface.hardwareAddress();
          ui->comboBoxTCPHostIp->addItem(entry.ip().toString());
        }
      }
      ui->comboBoxTCPHostIp->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    }
  }

  settingsIni->beginGroup("MAINSETTINGS");
  QString hostIp = settingsIni->value("cbHostAddress").toString();
  settingsIni->endGroup();

  for (int var = 0; var < ui->comboBoxTCPHostIp->count(); ++var) {
    if (hostIp == ui->comboBoxTCPHostIp->itemText(var)) {
      ui->comboBoxTCPHostIp->setCurrentIndex(var);
      break;
    }
  }
  cbHostAddress = ui->comboBoxTCPHostIp->currentText();
}
void MainWindow::setConsol() {
  QFont font("Monospace");
  font.setStyleHint(QFont::TypeWriter);
  ui->console->setFont(font);
  ui->console->setFrameShape(QFrame::NoFrame);

  QPlainTextEdit *out;
  out = ui->console;
  out->setContextMenuPolicy(Qt::CustomContextMenu);

  //-------------1-------------
  consolKeepAliveVisibale = new QAction(tr("Show keep-alive info"), this);
  consolKeepAliveVisibale->setStatusTip(tr("hide or show information about keep-alive request"));
  consolKeepAliveVisibale->setCheckable(true);
  consolKeepAliveVisibale->setChecked(consolLevelOutput[msg_alive]);
  //-------------2-------------
  consolMainVisibale = new QAction(tr("Show main info"), this);
  consolMainVisibale->setStatusTip(tr("hide or show information about main activity"));
  consolMainVisibale->setCheckable(true);
  consolMainVisibale->setChecked(consolLevelOutput[msg_main]);
  //-------------3-------------
  consolNetworkVisibale = new QAction(tr("Show network info"), this);
  consolNetworkVisibale->setStatusTip(tr("hide or show information about network communication"));
  consolNetworkVisibale->setCheckable(true);
  consolNetworkVisibale->setChecked(consolLevelOutput[msg_tcp]);

  QObject::connect(out, &QTableWidget::customContextMenuRequested, this, [this]() {
    QMenu *consolMenu = new QMenu;

    consolMenu->addAction(consolKeepAliveVisibale);
    consolMenu->addAction(consolMainVisibale);
    consolMenu->addAction(consolNetworkVisibale);

    consolMenu->exec(QCursor::pos());
    consolMenu->clear();
  });
  QObject::connect(consolKeepAliveVisibale, &QAction::triggered, this, [this]() {
    consolLevelOutput[msg_alive] = !consolLevelOutput[msg_alive];
    settingsIni->beginGroup("MAINSETTINGS");
    settingsIni->setValue("consolKeepAliveVisibale", consolLevelOutput[msg_alive]);
    settingsIni->endGroup();
    settingsIni->sync();
  });
  QObject::connect(consolMainVisibale, &QAction::triggered, this, [this]() {
    consolLevelOutput[msg_main] = !consolLevelOutput[msg_main];
    settingsIni->beginGroup("MAINSETTINGS");
    settingsIni->setValue("consolMainVisibale", consolLevelOutput[msg_main]);
    settingsIni->endGroup();
    settingsIni->sync();
  });
  QObject::connect(consolNetworkVisibale, &QAction::triggered, this, [this]() {
    consolLevelOutput[msg_tcp] = !consolLevelOutput[msg_tcp];
    settingsIni->beginGroup("MAINSETTINGS");
    settingsIni->setValue("consolNetworkVisibale", consolLevelOutput[msg_tcp]);
    settingsIni->endGroup();
    settingsIni->sync();
  });
}
void MainWindow::printToConsol(QString data, int level) {
  QString timeHtml = "<font color=\"DeepPink\">";
  QString mainHtml = "<font color=\"Aqua\">";
  QString tcpHtml = "<font color=\"Lime\">";
  QString defHtml = "<font color=\"White\">";

  switch (level) {
  case msg_alive:
    if (!consolLevelOutput[msg_alive])
      return;
    data.prepend(timeHtml);
    break;
  case msg_main:
    if (!consolLevelOutput[msg_main])
      return;
    data.prepend(mainHtml);
    break;
  case msg_tcp:
    if (!consolLevelOutput[msg_tcp])
      return;
    data.prepend(tcpHtml);
    break;
  default:
    data.prepend(defHtml);
    break;
  }
  ui->console->appendHtml(timeHtml + QDateTime::currentDateTime().toString("hh:mm:ss:zzz") + defHtml + ":  " + data);
}
void MainWindow::loadIni() {
  QString curPath;
  curPath = QCoreApplication::applicationDirPath() + "/settings.ini";
  if (!QDir(ui->leDefaultMediaFolderPath->text()).exists()) {
    //      curPath = ui->leDefaultMediaFolderPath->text();
  }

  settingsIni = new QSettings(curPath, QSettings::IniFormat);

  // this->blockSignals(true);
  settingsIni->beginGroup("MAINSETTINGS");
  ui->lineEditTCPport->setText(settingsIni->value("TCPport").toString());
  ui->lineEdit_2->setText(settingsIni->value("clientMediaPath").toString());
  ui->lineEdit_3->setText(settingsIni->value("clientPath").toString());
  // ui->comboBoxTCPHostIp->setCurrentText(settingsIni->value("TCPport").toString());

  this->setGeometry(settingsIni->value("mWindowPosX").toInt(), settingsIni->value("mWindowPosY").toInt(), settingsIni->value("mWindowW").toInt(),
                    settingsIni->value("mWindowH").toInt());
  ui->splitter->restoreState(settingsIni->value("consolSplitPos").toByteArray());
  ui->splitter_2->restoreState(settingsIni->value("scenarioSplitPos").toByteArray());
  ui->freqKeepAliveReq->setValue(settingsIni->value("TCPreqFreq").toInt());
  ui->numKeepAliveReq->setValue(settingsIni->value("TCPreqNum").toInt());
  freqKeepAliveReq = ui->freqKeepAliveReq->value();
  numKeepAliveReq = ui->numKeepAliveReq->value();

  ui->tabWidget->setCurrentIndex(settingsIni->value("activeTab").toInt());
  ui->uiAnimation->setChecked(settingsIni->value("uiAnimation").toBool());

  consolLevelOutput[msg_alive] = settingsIni->value("consolKeepAliveVisibale").toBool();
  consolLevelOutput[msg_main] = settingsIni->value("consolMainVisibale").toBool();
  consolLevelOutput[msg_tcp] = settingsIni->value("consolNetworkVisibale").toBool();

  ui->checkBox->setChecked(settingsIni->value("KeepAliveEnable").toBool());
  KeepAliveEnable = ui->checkBox->checkState();
  if (KeepAliveEnable) {
    ui->freqKeepAliveReq->setEnabled(true);
    ui->numKeepAliveReq->setEnabled(true);
  } else {
    ui->freqKeepAliveReq->setEnabled(false);
    ui->numKeepAliveReq->setEnabled(false);
  }

  ui->leDefaultMediaFolderPath->setText(settingsIni->value("defaultMediaFolderPath").toString());
  ui->lePythonPath->setText(settingsIni->value("lePythonPath").toString());
  settingsIni->endGroup();

  QStringList tNames;
  tNames << "TLIGHT1"
         << "TLIGHT2"
         << "TLIGHT3"
         << "TLIGHT4";
  ui->comboBox->addItems(tNames);
  for (int var = 0; var < 4; ++var) {
    settingsIni->beginGroup(tNames.at(var));
    TLightSettings[var].gifRows = settingsIni->value("gifRows").toInt();
    TLightSettings[var].gifRols = settingsIni->value("gifRols").toInt();
    TLightSettings[var].chain_length = settingsIni->value("chain_length").toInt();
    TLightSettings[var].parallel = settingsIni->value("parallel").toInt();
    TLightSettings[var].scan_mode = settingsIni->value("scan_mode").toInt();
    TLightSettings[var].multiplexing = settingsIni->value("multiplexing").toInt();
    TLightSettings[var].led_rgb_sequence = settingsIni->value("led_rgb_sequence").toString();
    TLightSettings[var].pwm_bits = settingsIni->value("pwm_bits").toInt();
    TLightSettings[var].brightness = settingsIni->value("brightness").toInt();
    TLightSettings[var].gpio_slowdown = settingsIni->value("gpio_slowdown").toInt();
    TLightSettings[var].delay = settingsIni->value("delay").toInt();
    TLightSettings[var].rotate = settingsIni->value("rotate").toInt();
    settingsIni->endGroup();

    ui->spinBox->setValue(TLightSettings[var].gifRows);
    ui->spinBox_2->setValue(TLightSettings[var].gifRols);
    ui->spinBox_3->setValue(TLightSettings[var].chain_length);
    ui->spinBox_4->setValue(TLightSettings[var].parallel);
    ui->spinBox_5->setValue(TLightSettings[var].scan_mode);
    ui->spinBox_6->setValue(TLightSettings[var].multiplexing);
    ui->lineEdit->setText(TLightSettings[var].led_rgb_sequence);
    ui->spinBox_8->setValue(TLightSettings[var].pwm_bits);
    ui->spinBox_9->setValue(TLightSettings[var].brightness);
    ui->spinBox_7->setValue(TLightSettings[var].gpio_slowdown);
    ui->spinBox_10->setValue(TLightSettings[var].delay);
    ui->spinBox_11->setValue(TLightSettings[var].rotate);
  }
  // this->blockSignals(false);
}
void MainWindow::oopSmessage() {
  std::random_shuffle(oppsList->begin(), oppsList->end());
  if (oppsList->isEmpty())
    oppsList = oppsListAll;
  sorry->setText(oppsList->takeFirst());
  sorry->exec();
}
void MainWindow::mainSetup() {
  sorry = new QMessageBox;
  sorry->addButton(QMessageBox::Ok);
  QPixmap pmap(":/myMedia/oops.png");
  QSize size(70, 70);
  pmap = pmap.scaled(size);
  // ptr->setIconSize(size);
  // sorry->setIcon(pmap);
  sorry->setIconPixmap(pmap);

  oppsList = new QStringList;
  oppsListAll = new QStringList;
  oppsListAll->append("It was working yesterday, I swear!");
  oppsListAll->append("That doesn't happen on my machine.");
  oppsListAll->append("That shouldn’t happen.");
  oppsListAll->append("Oops!");
  oppsListAll->append("Upgrade to Pro Version");
  oppsList = oppsListAll;

  QWidget *widget = new QWidget();
  hideConsole = new QPushButton("hide console");
  hideConsole->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
  QObject::connect(hideConsole, &QPushButton::clicked, this, [this]() {
    if (hideConsole->text() == "hide console") {
      hideConsole->setText("show console");
      hideConsole->setStyleSheet("background-color: rgb(20, 168, 37)");
    } else {
      hideConsole->setText("hide console");
      hideConsole->setStyleSheet("background-color: rgb(214, 27, 27)");
    }
    oopSmessage();
  });

  hideScenario = new QPushButton("hide scenario");
  hideScenario->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
  QObject::connect(hideScenario, &QPushButton::clicked, this, [this]() {
    if (hideScenario->text() == "hide scenario") {
      hideScenario->setText("show scenario");
      hideScenario->setStyleSheet("background-color: rgb(20, 168, 37)");
    } else {
      hideScenario->setText("hide scenario");
      hideScenario->setStyleSheet("background-color: rgb(214, 27, 27)");
    }
    oopSmessage();
  });

  QLabel *rightLable = new QLabel;
  rightLable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  rightLable->setText("<a href=\"www\" style=\"color: rgb(0, 200, 255);\">Designed by: snisarenko.dmitry@gmail.com</a>");
  rightLable->setTextInteractionFlags(Qt::TextBrowserInteraction);
  //  rightLable->setOpenExternalLinks(true);
  QObject::connect(rightLable, &QLabel::linkActivated, this, []() { QDesktopServices::openUrl(QUrl("http://www.facebook.com/dmitry.snisarenko/")); });

  QGridLayout *layout = new QGridLayout(widget);
  layout->setMargin(0);
  layout->setSpacing(0);
  //  QSpacerItem spacer = new QSpacerItem();

  layout->addWidget(hideConsole, 0, 0, 1, 1, Qt::AlignVCenter | Qt::AlignLeft);
  layout->addWidget(hideScenario, 0, 1, 1, 1, Qt::AlignVCenter | Qt::AlignLeft);
  //  layout->addItem(spacer, 0, 2, 1, 1, Qt::AlignVCenter | Qt::AlignRight);
  layout->setColumnStretch(2, 1);
  layout->addWidget(rightLable, 0, 3, 1, 1, Qt::AlignVCenter | Qt::AlignRight);
  ui->statusBar->addWidget(widget, 1);
}
void MainWindow::setupUI() {
  mainSetup();
  // output consol
  setConsol();
  // widget tabs(header)
  tabWidgetSetup();
  // tabs
  tabTraficLightsSetup();
  tabNetInfoSetup();
  tabAppConfigSetup();
}
void MainWindow::setAnimationToMatrix(QPushButton *ptr, int k, int i, int j) {
  QString text = QStringLiteral("(%1,%2,%3)").arg(k).arg(i).arg(j);
  printToConsol(QString(text), msg_main);
  QString curPath;
  if (QDir(ui->leDefaultMediaFolderPath->text()).exists()) {
    curPath = ui->leDefaultMediaFolderPath->text();
  } else {
    return;
    //    curPath = QDir::currentPath();
  }
  QString filePath = QFileDialog::getOpenFileName(this, "Open Document", curPath, "GIF files (*.gif)");
  QString fileName = QFileInfo(filePath).baseName();

  if (!filePath.isNull()) {
    //  qDebug() << fileName;
    ptr->setProperty("gifFileName", fileName);
    printToConsol(QString(filePath), msg_main);
    QPixmap pmap(filePath);
    QSize size(ptr->size().width(), ptr->size().height());
    pmap = pmap.scaled(size);
    ptr->setIconSize(size);
    ptr->setIcon(pmap);
  }
}
void MainWindow::fillNetworkDevicesTables() {
  QTableWidget *table;
  table = ui->tableWidgetNetworkDevices;
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setEnabled(true);
  //    table->setSelectionMode(QAbstractItemView::NoSelection);
  //    table->setEditTriggers( QAbstractItemView::NoEditTriggers);
  QStringList tableColNames;
  tableColNames << "GroupName"
                << "SubgroupName"
                << "IP"
                << "Descriptor"
                << "Connected time"
                << "Disconnected time"
                << "State"
                << "Count of RX msg"
                << "Count of TX msg";

  table->setColumnCount(tableColNames.size());
  table->setRowCount(0);

  for (int var = 0; var < tableColNames.size(); ++var) {
    table->setHorizontalHeaderItem(var, new QTableWidgetItem(tableColNames.at(var)));
  }

  int rows = sizeof riddle->dev / sizeof riddle->dev[0];
  for (int i = 0; i < rows; i++) {
    table->setRowCount(i + 1);
    int col = 100;

    table->setItem(i, 0, new QTableWidgetItem(riddle->dev[i].GroupName));
    table->item(i, 0)->setTextColor(QColor(col, col, col));
    table->setItem(i, 1, new QTableWidgetItem(riddle->dev[i].SubGroupName));
    table->item(i, 1)->setTextColor(QColor(col, col, col));
    table->setItem(i, 6, new QTableWidgetItem(riddle->dev[i].state));
    if (riddle->dev[i].state == "offline") {
      table->item(i, 6)->setTextColor(QColor(200, 0, 0));
    } else {
      table->item(i, 6)->setTextColor(QColor(0, 200, 0));
    }
  }

  table->resizeColumnsToContents();
  table->setSelectionBehavior(QAbstractItemView::SelectItems);
  table->setSelectionMode(QAbstractItemView::NoSelection);
}
void MainWindow::on_actionSave_and_quit_triggered() {
  on_actionSave_triggered();
  qApp->quit();
}
void MainWindow::on_actionSave_triggered() {
  settingsIni->beginGroup("MAINSETTINGS");
  settingsIni->setValue("mWindowH", ui->centralWidget->size().height());
  settingsIni->setValue("mWindowW", ui->centralWidget->size().width());
  settingsIni->setValue("mWindowPosX", this->pos().x());
  settingsIni->setValue("mWindowPosY", this->pos().y());
  settingsIni->setValue("mWindowPosY", this->pos().y());
  settingsIni->setValue("consolSplitPos", ui->splitter->saveState());
  //  settingsIni->setValue("scenarioSplitPos", ui->splitter_2->saveState());
  settingsIni->setValue("consolSplitHide", hideConsole->text());
  settingsIni->setValue("scenarioSplitHide", hideScenario->text());
  settingsIni->setValue("TCPport", ui->lineEditTCPport->text());
  settingsIni->setValue("TCPreqFreq", ui->freqKeepAliveReq->value());
  settingsIni->setValue("TCPreqNum", ui->numKeepAliveReq->value());
  settingsIni->setValue("activeTab", ui->tabWidget->currentIndex());
  settingsIni->setValue("defaultMediaFolderPath", ui->leDefaultMediaFolderPath->text());
  settingsIni->setValue("lePythonPath", ui->lePythonPath->text());
  settingsIni->setValue("uiAnimation", ui->uiAnimation->isChecked());
  settingsIni->setValue("cbHostAddress", ui->comboBoxTCPHostIp->currentText());
  settingsIni->setValue("KeepAliveEnable", ui->checkBox->isChecked());
  settingsIni->setValue("clientMediaPath", ui->lineEdit_2->text());
  settingsIni->setValue("clientPath", ui->lineEdit_3->text());

  settingsIni->endGroup();
  settingsIni->sync();
}

void MainWindow::on_leNewTimeEvent_textEdited(const QString &arg1) {
  if (arg1.length() == 8 && arg1 != "00:00:00")
    ui->pbAddEvent->setEnabled(true);
  else
    ui->pbAddEvent->setEnabled(false);
}
void MainWindow::on_pbAddEvent_clicked() {
  MyTempScenario tempScenario;
  QString newTime = ui->leNewTimeEvent->text();
  QTableWidget *table;
  table = ui->tableWidgetScenario;

  if (table->rowCount() > maxScenarioNum || traficLightStructList.size() > maxScenarioNum) {
    return;
  }

  for (int k = 0; k < 4; ++k) {
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 5; ++j) {
        QString name = button[k][i][j]->property("gifFileName").toString();
        if (name.size() > 0)
          tempScenario.traficLight[k].append(name + separator);
        else
          tempScenario.traficLight[k].append(separator);

        button[k][i][j]->setProperty("gifFileName", "");
        QPixmap pmap;
        button[k][i][j]->setIcon(pmap);
      }
    }
  }
  tempScenario.awaitTime = newTime;

  int rowNum = table->rowCount();
  table->setRowCount(rowNum + 1);
  //  if (ui->checkBox_2->isChecked()) {
  //    secretScenarios->push_back(table->rowCount());
  //  }
  //  table->setItem(rowNum, 0, new QTableWidgetItem(QString::number(rowNum)));
  table->setItem(rowNum, 0, new QTableWidgetItem(tempScenario.scenarioName));
  table->setItem(rowNum, 1, new QTableWidgetItem(tempScenario.awaitTime));
  table->setItem(rowNum, 2, new QTableWidgetItem(tempScenario.traficLight[0]));
  table->setItem(rowNum, 3, new QTableWidgetItem(tempScenario.traficLight[1]));
  table->setItem(rowNum, 4, new QTableWidgetItem(tempScenario.traficLight[2]));
  table->setItem(rowNum, 5, new QTableWidgetItem(tempScenario.traficLight[3]));
  //  traficLightStructList[rowNum].UniconLight = ui->checkBox_3->checkState();
  //  traficLightStructList[rowNum].SecretScenario = ui->checkBox_2->checkState();
  //  table->setItem(rowNum, 6, new QTableWidgetItem(tempScenario.UniconLight));
  //  table->setItem(rowNum, 7, new QTableWidgetItem(tempScenario.SecretScenario));

  //  // add QCheckBox in cell
  //  QWidget *wdg = new QWidget;         // first create a new widget
  //  QCheckBox *ccBox = new QCheckBox(); // lets say we want a checkbox in the cell
  //  ccBox->setProperty("raw", (int)rowNum);
  //  ccBox->setCheckState(ui->checkBox_2->checkState());
  //  QHBoxLayout *layout = new QHBoxLayout(wdg); // create the layout ON THE HEAP! (else after the function ends, layout is gone)
  //  layout->addWidget(ccBox);
  //  connect(ccBox, SIGNAL(stateChanged(int)), this, SLOT(on_UniconSateChanged(int))); // add the checkbox to the widget
  //  layout->setAlignment(Qt::AlignCenter); // center align the layout (box ends up in the center horizontally and vertically)
  //  layout->setMargin(0);
  //  wdg->setLayout(layout);               // set the layout on the widget, it takes ownership of the layout (don't need to delete it later)
  //  table->setCellWidget(rowNum, 6, wdg); // targetStructList[i].cbStTarget);

  //  // add QCheckBox in cell
  //  QWidget *wdg2 = new QWidget;         // first create a new widget
  //  QCheckBox *ccBox2 = new QCheckBox(); // lets say we want a checkbox in the cell
  //  ccBox2->setProperty("raw", (int)rowNum);
  //  ccBox2->setCheckState(ui->checkBox_3->checkState());
  //  connect(ccBox2, SIGNAL(stateChanged(int)), this, SLOT(on_SecretSateChanged(int)));
  //  QHBoxLayout *layout2 = new QHBoxLayout(wdg2); // create the layout ON THE HEAP! (else after the function ends, layout is gone)
  //  layout2->addWidget(ccBox2);                   // add the checkbox to the widget
  //  layout2->setAlignment(Qt::AlignCenter);       // center align the layout (box ends up in the center horizontally and vertically)
  //  layout2->setMargin(0);
  //  wdg2->setLayout(layout2);              // set the layout on the widget, it takes ownership of the layout (don't need to delete it later)
  //  table->setCellWidget(rowNum, 7, wdg2); // targetStructList[i].cbStTarget);

  //  table->item(rowNum, 0)->setFlags(table->item(rowNum, 0)->flags() & ~Qt::ItemIsEditable);
  //  table->item(rowNum, 1)->setFlags(table->item(rowNum, 0)->flags() & ~Qt::ItemIsEditable);
  //  table->item(rowNum, 2)->setFlags(table->item(rowNum, 0)->flags() & ~Qt::ItemIsEditable);
  //  table->item(rowNum, 3)->setFlags(table->item(rowNum, 0)->flags() & ~Qt::ItemIsEditable);
  //  table->item(rowNum, 4)->setFlags(table->item(rowNum, 0)->flags() & ~Qt::ItemIsEditable);
  //  table->item(rowNum, 5)->setFlags(table->item(rowNum, 0)->flags() & ~Qt::ItemIsEditable);

  table->resizeColumnsToContents();
  table->setSelectionBehavior(QAbstractItemView::SelectItems);
  table->setSelectionMode(QAbstractItemView::NoSelection);

  QString text = QStringLiteral("new event time set: %1").arg(newTime);
  printToConsol(text, msg_main);
}
void MainWindow::on_tableWidgetScenario_itemChanged(QTableWidgetItem *item) {
  QTableWidget *table;
  table = ui->tableWidgetScenario;
  table->resizeColumnsToContents();
  if (traficLightStructList.size() <= item->row()) {
    MyTraficLightStruct traficLightStruct;
    traficLightStructList.append(traficLightStruct);
  }

  QString value;
  if (!item || item->text().isEmpty()) {
    value = "";
  } else {
    value = item->text();
  }

  int j = item->column();
  if (j == 0)
    traficLightStructList[item->row()].scenarioName = value;
  else if (j == 1)
    traficLightStructList[item->row()].awaitTime = value;
  else if (j == 2 || j == 3 || j == 4 || j == 5) {
    QStringList strList = value.split(separator);
    if (j > 1 && j < 6) {
      for (int var = 0; var < strList.size() - 1; var++) {
        traficLightStructList[item->row()].traficLight[j - 2][var / 5][var - 5 * (int(var / 5))] = strList.at(var);
      }
    }
  } else if (j == 6) {
    // traficLightStructList[item->row()].UniconLight
  }
  // qSort(targetStructList);
  saveAsCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
  readFromCSV(QCoreApplication::applicationDirPath() + "/scenario.scv");
}
void MainWindow::on_pushButton_clicked() {
  QString dir;
  QString curPath;
  if (QDir(ui->leDefaultMediaFolderPath->text()).exists()) {
    curPath = ui->leDefaultMediaFolderPath->text();
  } else {
    curPath = QDir::currentPath();
  }
  dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"), curPath, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  ui->leDefaultMediaFolderPath->setText(dir);
}
void MainWindow::on_pushButton_2_clicked() {
  for (int k = 0; k < 4; ++k) {
    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 5; ++j) {
        button[k][i][j]->setProperty("gifFileName", "");
        QPixmap pmap;
        button[k][i][j]->setIcon(pmap);
      }
    }
  }
}
//---------Server---------
void MainWindow::send_by_name(QString cur_nodeName, QString cur_targetName, QByteArray cmd) {
  qDebug() << cur_nodeName << cur_targetName << cmd;
  int cnt = sizeof riddle->riddleList / sizeof riddle->riddleList[0];
  for (int i = 0; i < cnt; i++) {
    if (riddle->dev[i].GroupName == cur_nodeName && riddle->dev[i].SubGroupName == cur_targetName) {
      if (riddle->dev[i].descriptor != 0) {
        // qDebug()<<"send_by_name";
        emit SendCmdClient(riddle->dev[i].descriptor, cmd);
      }
    }
  }
}
void MainWindow::on_UpdateDevicesList(QList<QTcpSocket *> &sock_list) {
  QTableWidget *table;
  table = ui->tableWidgetNetworkDevices;
  // 1. если есть новый клиент но он еще не авторизирован то добавляем его в
  // верх списка с маркером UNKNOWN DEV
  foreach (QTcpSocket *client, sock_list) {
    bool f = true;
    //проверяем совпадает ли данный адресс клиента с адрессами в списке
    //авторизированных устройствах
    int cnt1 = sizeof riddle->riddleList / sizeof riddle->riddleList[0];
    for (int i = 0; i < cnt1; i++) {
      if (client->peerAddress() == riddle->dev[i].ip) {
        f = false;
      }
    }
    //проверяем есть ли он уже на экране
    int cnt2 = table->rowCount();
    for (int i = 0; i < cnt2; i++) {
      if (!table->item(i, 2)) {
      } else {
        if (client->peerAddress().toString().mid(0) == table->item(i, 2)->text()) {
          f = false;
        }
      }
    }
    if (f) {
      table->insertRow(0);
      table->setItem(0, 0, new QTableWidgetItem("UNKNOWN"));
      table->setItem(0, 1, new QTableWidgetItem("UNKNOWN"));
      table->setItem(0, 2, new QTableWidgetItem(client->peerAddress().toString().mid(0)));
      table->setItem(0, 3, new QTableWidgetItem(QString::number(client->socketDescriptor())));
    }
  }

  // 2. если не авторизированный клиент ответил на запрос "WHO ARE YOU" то
  // появилась связь его имени и его ip;
  //    удаляем его из списка неавторизированных клиентов и добавляем его данные
  //    в авторизированнйх
  QList<int> row_remove;
  int cnt = table->rowCount();
  for (int i = 0; i < cnt; i++) {
    //проверяем все строки на наличие неавторизированного клиента
    if (table->item(i, 0)->text() == "UNKNOWN" && table->item(i, 1)->text() == "UNKNOWN") {
      int cnt2 = sizeof riddle->dev / sizeof riddle->dev[0];
      for (int j = 0; j < cnt2; j++) {
        // проверяем что найденный неавторизированный клиент уже имеется в
        // списки устройств
        if (riddle->dev[j].ip.toString().mid(0) == table->item(i, 2)->text() && riddle->dev[j].ip.toString().mid(0) != "") {
          for (int ii = 0; ii < cnt; ii++) {
            if (table->item(ii, 0)->text() == riddle->dev[j].GroupName && table->item(ii, 1)->text() == riddle->dev[j].SubGroupName) {
              table->item(ii, 0)->setTextColor(QColor(239, 240, 241));
              table->item(ii, 1)->setTextColor(QColor(239, 240, 241));
              table->setItem(ii, 2, new QTableWidgetItem(riddle->dev[j].ip.toString().mid(0)));
              table->setItem(ii, 3, new QTableWidgetItem(QString::number(riddle->dev[j].descriptor)));

              table->setItem(ii, 4, new QTableWidgetItem(QDateTime::currentDateTime().toString("hh:mm:ss:zzz")));
              table->item(ii, 4)->setTextColor(QColor(239, 240, 241));
              if (table->item(ii, 5))
                table->item(ii, 5)->setTextColor(QColor(239, 240, 241));

              table->setItem(ii, 6, new QTableWidgetItem(riddle->dev[j].state));
              if (riddle->dev[j].state == "offline") {
                table->item(ii, 6)->setTextColor(QColor(200, 0, 0));
              } else {
                table->item(ii, 6)->setTextColor(QColor(0, 200, 0));
              }
              // ui->tableWidget->setItem(ii,4, new
              // QTableWidgetItem(rid->dev[j].dev_state));
            }
          }
          row_remove.push_back(i);
          table->setRowHeight(i, 20);
          for (int k = 0; k < table->columnCount(); k++) {
            table->setItem(i, k, new QTableWidgetItem(""));
          }
        }
      }
    }
  }

  // 3. если клиент отключился
  cnt = table->rowCount();
  for (int i = 0; i < cnt; i++) {
    QTableWidgetItem *item_check = table->item(i, 2);
    if (item_check && !item_check->text().isEmpty()) {
      //проверяем все строки на наличие клиента которого уже нет в списке
      //подключенных
      bool f_online = false;
      foreach (QTcpSocket *client, sock_list) {
        //        if (table->item(i, 2)->text() == client->peerAddress().toString()) {
        if (table->item(i, 2)->text() == client->peerAddress().toString().mid(0)) {
          f_online = true;
        }
      }
      if (f_online == false) {
        int col = 100;
        table->item(i, 0)->setTextColor(QColor(col, col, col));
        table->item(i, 1)->setTextColor(QColor(col, col, col));
        table->setItem(i, 2, new QTableWidgetItem(""));
        table->setItem(i, 3, new QTableWidgetItem(""));

        if (table->item(i, 4)) {
          table->item(i, 4)->setTextColor(QColor(col, col, col));
        }

        table->setItem(i, 5, new QTableWidgetItem(QDateTime::currentDateTime().toString("hh:mm:ss:zzz")));
        table->item(i, 5)->setTextColor(QColor(col, col, col));

        table->setItem(i, 6, new QTableWidgetItem("offline"));
        table->item(i, 6)->setTextColor(QColor(200, 0, 0));
      }
    }
  }

  // 4. удаляем пустые строки
  if (row_remove.size() > 0) {
    for (int i = 0; i < row_remove.size(); i++) {
      table->removeRow(row_remove.at(i));
    }
  }
  table->resizeColumnsToContents();
}
void MainWindow::_newParsedMsg(QTcpSocket *cl, QByteArray buf) { riddle->newParsedMsg(cl, buf); }
void MainWindow::on_freqKeepAliveReq_valueChanged(int arg1) {
  settingsIni->setValue("TCPreqFreq", ui->freqKeepAliveReq->value());
  freqKeepAliveReq = ui->freqKeepAliveReq->value();
}
void MainWindow::on_numKeepAliveReq_valueChanged(int arg1) {
  settingsIni->setValue("TCPreqNum", ui->numKeepAliveReq->value());
  numKeepAliveReq = ui->numKeepAliveReq->value();
}
void MainWindow::on_pushButton_3_clicked() {
  ui->pushButton_3->setEnabled(false);
  PopUpWindow = new Dialog(this);
  PopUpWindow->set_btnEnable("OK", false);
  PopUpWindow->setModal(true);
  PopUpWindow->show();
  // PopUpWindow->exec();

  QStringList clientList;
  clientList << "192.168.88.101";
  clientList << "192.168.88.102";
  clientList << "192.168.88.103";
  clientList << "192.168.88.104";
  clientList << "192.168.88.105";

  PopUpWindow->set_text("Try copy to clients:");
  QString str = clientList.join("\n");
  PopUpWindow->set_text(str);
  PopUpWindow->set_text("\nplease wait ...\n");

  tftp = new QProcess;
  QStringList optionList_tftp;
  tftp->setProcessChannelMode(QProcess::MergedChannels);
  //    optionList_tftp << QCoreApplication::applicationDirPath() + "/include/script/pySFTP.py";
  //    optionList_tftp << "-f"
  //                    << "/Users/Dmitry/GoogleDrive/Freelance/Quest/BurningMen/Qt/media/gif/";
  //    optionList_tftp << "-t"
  //                    << "/home/pi/media/gif/";
  optionList_tftp << QCoreApplication::applicationDirPath() + "/include/script/pySFTP.py";
  optionList_tftp << "-f" << ui->leDefaultMediaFolderPath->text() + "/";
  optionList_tftp << "-t" << ui->lineEdit_2->text() + "/gif/";
  foreach (QString str, clientList) { optionList_tftp << "-i" << str; }
  QDir directory(ui->leDefaultMediaFolderPath->text());
  QStringList images = directory.entryList(QStringList() << "*.gif", QDir::Files);
  foreach (QString filename, images) { optionList_tftp << "-a" << filename; }

  qDebug() << optionList_tftp;

  tftp->waitForFinished(-1);

  //    tftp->start("python", optionList_tftp);
  tftp->start(ui->lePythonPath->text(), optionList_tftp);

  //    ssh = new QProcess(this);
  //    QStringList optionList_ssh;
  //    ssh->setProcessChannelMode(QProcess::MergedChannels);
  //    QString cmd=ui->lePythonPath->text()+" "+QCoreApplication::applicationDirPath() + "/include/script/pySSH.py -s 1";
  //    ssh->waitForFinished(-1);
  //    ssh->start(cmd);

  connect(tftp, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), [=](int exitCode, QProcess::ExitStatus exitStatus) {
    PopUpWindow->set_btnEnable("OK", true);
    ui->pushButton_3->setEnabled(true);
    PopUpWindow->set_text("sftp output:");
    PopUpWindow->set_text(tftp->readAll());
    PopUpWindow->set_text("All files have been copied successfully\n");
    PopUpWindow->set_text("---------------------------------------\n\n");
    tftp->deleteLater();
  });
  //  if (clientList.size() > 0) {
  //    PopUpWindow->set_text("Online clients:");
  //    QString str = clientList.join("\n");
  //    PopUpWindow->set_text(str);
  //    PopUpWindow->set_text("\nplease wait ...\n");

  //    tftp = new QProcess;
  //    QStringList optionList_tftp;
  //    tftp->setProcessChannelMode(QProcess::MergedChannels);
  //    //    optionList_tftp << QCoreApplication::applicationDirPath() + "/include/script/pySFTP.py";
  //    //    optionList_tftp << "-f"
  //    //                    << "/Users/Dmitry/GoogleDrive/Freelance/Quest/BurningMen/Qt/media/gif/";
  //    //    optionList_tftp << "-t"
  //    //                    << "/home/pi/media/gif/";
  //    optionList_tftp << QCoreApplication::applicationDirPath() + "/include/script/pySFTP.py";
  //    optionList_tftp << "-f"
  //                    << ui->leDefaultMediaFolderPath->text()+"/";
  //    optionList_tftp << "-t"
  //                    << ui->lineEdit_2->text()+"/gif/";
  //    foreach (QString str, clientList) { optionList_tftp << "-i" << str; }
  //    QDir directory(ui->leDefaultMediaFolderPath->text());
  //    QStringList images = directory.entryList(QStringList() << "*.gif", QDir::Files);
  //    foreach (QString filename, images) { optionList_tftp << "-a" << filename; }

  //    qDebug() << optionList_tftp;

  //    tftp->waitForFinished(-1);

  ////    tftp->start("python", optionList_tftp);
  //    tftp->start(ui->lePythonPath->text(), optionList_tftp);

  ////    ssh = new QProcess(this);
  ////    QStringList optionList_ssh;
  ////    ssh->setProcessChannelMode(QProcess::MergedChannels);
  ////    QString cmd=ui->lePythonPath->text()+" "+QCoreApplication::applicationDirPath() + "/include/script/pySSH.py -s 1";
  ////    ssh->waitForFinished(-1);
  ////    ssh->start(cmd);

  //    connect(tftp, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
  //            [=](int exitCode, QProcess::ExitStatus exitStatus) {
  //              PopUpWindow->set_btnEnable("OK", true);
  //              ui->pushButton_3->setEnabled(true);
  //              PopUpWindow->set_text("sftp output:");
  //              PopUpWindow->set_text(tftp->readAll());
  //              PopUpWindow->set_text("All files have been copied successfully\n");
  //              PopUpWindow->set_text("---------------------------------------\n\n");
  //              tftp->deleteLater();
  //            });
  //  } else {
  //    ui->pushButton_3->setEnabled(true);
  //    PopUpWindow->set_text("All clients offline!\n");
  //    PopUpWindow->set_btnEnable("OK", true);
  //  }
}
void MainWindow::on_comboBoxTCPHostIp_currentIndexChanged(const QString &arg1) { cbHostAddress = ui->comboBoxTCPHostIp->currentText(); }
void MainWindow::on_checkBox_stateChanged(int arg1) {
  KeepAliveEnable = arg1;
  if (arg1) {
    ui->freqKeepAliveReq->setEnabled(true);
    ui->numKeepAliveReq->setEnabled(true);
  } else {
    ui->freqKeepAliveReq->setEnabled(false);
    ui->numKeepAliveReq->setEnabled(false);
  }
}
void MainWindow::on_lineEdit_2_returnPressed() {
  settingsIni->beginGroup("MAINSETTINGS");
  settingsIni->setValue("clientMediaPath", ui->lineEdit_2->text());
  settingsIni->endGroup();
}
void MainWindow::on_pushButton_4_clicked() {
  /// Users/Dmitry/AppData/Local/Programs/Python/Python37-32/python
  QString dir;
  QString curPath;
  if (QDir(ui->lePythonPath->text()).exists()) {
    curPath = ui->lePythonPath->text();
  } else {
    curPath = QDir::currentPath();
  }
  dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"), curPath, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  ui->lePythonPath->setText(dir + "/python");
}
void MainWindow::on_pushButton_5_clicked() {
  QString file;
  QString dir;
  QString curPath;
  if (QDir(ui->lineEdit_3->text()).exists()) {
    curPath = ui->lineEdit_3->text();
  } else {
    curPath = QDir::currentPath();
  }
  // file = QFileDialog::getOpenFileName(this, tr("Open File"), curPath);
  dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"), curPath, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  ui->lineEdit_3->setText(dir);
}
void MainWindow::on_pushButton_6_clicked() {
  PopUpWindow = new Dialog(this);
  PopUpWindow->set_btnEnable("OK", false);
  PopUpWindow->setModal(true);
  PopUpWindow->show();
  // PopUpWindow->exec();

  QStringList clientList;
  clientList << "192.168.88.101";
  clientList << "192.168.88.102";
  clientList << "192.168.88.103";
  clientList << "192.168.88.104";
  clientList << "192.168.88.105";

  PopUpWindow->set_text("Try copy to clients:");
  QString str = clientList.join("\n");
  PopUpWindow->set_text(str);
  PopUpWindow->set_text("\nplease wait ...\n");

  tftp = new QProcess;
  QStringList optionList_tftp;
  tftp->setProcessChannelMode(QProcess::MergedChannels);

  optionList_tftp << QCoreApplication::applicationDirPath() + "/include/script/pySFTP.py";
  optionList_tftp << "-f" << ui->lineEdit_3->text() + "/";
  optionList_tftp << "-t" << ui->lineEdit_2->text() + "/";
  foreach (QString str, clientList) { optionList_tftp << "-i" << str; }
  optionList_tftp << "-a"
                  << "client";

  qDebug() << optionList_tftp;

  tftp->waitForFinished(-1);
  tftp->start(ui->lePythonPath->text(), optionList_tftp);

  connect(tftp, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), [=](int exitCode, QProcess::ExitStatus exitStatus) {
    PopUpWindow->set_btnEnable("OK", true);
    ui->pushButton_3->setEnabled(true);
    PopUpWindow->set_text("sftp output:");
    PopUpWindow->set_text(tftp->readAll());
    PopUpWindow->set_text("All files have been copied successfully\n");
    PopUpWindow->set_text("---------------------------------------\n\n");
    tftp->deleteLater();
  });
}
void MainWindow::on_pushButton_7_clicked() {
  QString name = ui->comboBox->currentText();
  int var = ui->comboBox->currentIndex();
  settingsIni->beginGroup(name);

  settingsIni->setValue("gifRows", ui->spinBox->value());
  settingsIni->setValue("gifRols", ui->spinBox_2->value());
  settingsIni->setValue("chain_length", ui->spinBox_3->value());
  settingsIni->setValue("parallel", ui->spinBox_4->value());
  settingsIni->setValue("scan_mode", ui->spinBox_5->value());
  settingsIni->setValue("multiplexing", ui->spinBox_6->value());
  settingsIni->setValue("led_rgb_sequence", ui->lineEdit->text());
  settingsIni->setValue("pwm_bits", ui->spinBox_8->value());
  settingsIni->setValue("brightness", ui->spinBox_9->value());
  settingsIni->setValue("gpio_slowdown", ui->spinBox_7->value());
  settingsIni->setValue("delay", ui->spinBox_10->value());
  settingsIni->setValue("rotate", ui->spinBox_11->value());

  TLightSettings[var].gifRows = settingsIni->value("gifRows").toInt();
  TLightSettings[var].gifRols = settingsIni->value("gifRols").toInt();
  TLightSettings[var].chain_length = settingsIni->value("chain_length").toInt();
  TLightSettings[var].parallel = settingsIni->value("parallel").toInt();
  TLightSettings[var].scan_mode = settingsIni->value("scan_mode").toInt();
  TLightSettings[var].multiplexing = settingsIni->value("multiplexing").toInt();
  TLightSettings[var].led_rgb_sequence = settingsIni->value("led_rgb_sequence").toString();
  TLightSettings[var].pwm_bits = settingsIni->value("pwm_bits").toInt();
  TLightSettings[var].brightness = settingsIni->value("brightness").toInt();
  TLightSettings[var].gpio_slowdown = settingsIni->value("gpio_slowdown").toInt();
  TLightSettings[var].delay = settingsIni->value("delay").toInt();
  TLightSettings[var].rotate = settingsIni->value("rotate").toInt();
  settingsIni->endGroup();
}
void MainWindow::on_comboBox_currentIndexChanged(const QString &arg1) {
  int var = ui->comboBox->currentIndex();
  ui->spinBox->setValue(TLightSettings[var].gifRows);
  ui->spinBox_2->setValue(TLightSettings[var].gifRols);
  ui->spinBox_3->setValue(TLightSettings[var].chain_length);
  ui->spinBox_4->setValue(TLightSettings[var].parallel);
  ui->spinBox_5->setValue(TLightSettings[var].scan_mode);
  ui->spinBox_6->setValue(TLightSettings[var].multiplexing);
  ui->lineEdit->setText(TLightSettings[var].led_rgb_sequence);
  ui->spinBox_8->setValue(TLightSettings[var].pwm_bits);
  ui->spinBox_9->setValue(TLightSettings[var].brightness);
  ui->spinBox_7->setValue(TLightSettings[var].gpio_slowdown);
  ui->spinBox_10->setValue(TLightSettings[var].delay);
  ui->spinBox_11->setValue(TLightSettings[var].rotate);
}
