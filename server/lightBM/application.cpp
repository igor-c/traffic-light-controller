#include "application.h"

Application::Application(int &argc, char **argv) : QApplication(argc, argv, true) { _singular = new QSharedMemory("TraficLightTCPServer", this); }

Application::~Application() {
  if (_singular->isAttached())
    _singular->detach();
}

bool Application::lock() {
  if (_singular->attach(QSharedMemory::ReadOnly)) {
    _singular->detach();
    return true;
  }

  if (_singular->create(1)) {
    return true;
  }
  return false;
}
