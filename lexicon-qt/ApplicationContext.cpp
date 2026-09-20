#include "ApplicationContext.h"

#include <cassert>

namespace {
QtApplicationFacade *activeApplication = nullptr;
}

void installApplication(QtApplicationFacade &application) {
  activeApplication = &application;
}

QtApplicationFacade &services() {
  assert(activeApplication != nullptr);
  return *activeApplication;
}
