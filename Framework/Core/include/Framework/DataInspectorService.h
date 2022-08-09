#ifndef O2_DATAINSPECTORSERVICE_H
#define O2_DATAINSPECTORSERVICE_H

#include <fairlogger/Logger.h>
#include "DISocket.hpp"

namespace o2::framework
{
class DataInspectorService {
 public:
  DataInspectorService(const std::string& deviceName);

  void receive();
  bool isInspected() {
    return _isInspected;
  }

 private:
  void handleMessage(DIMessage& msg);

  const std::string deviceName;
  bool _isInspected = false;
  DISocket socket;
};
}

#endif //O2_DATAINSPECTORSERVICE_H
