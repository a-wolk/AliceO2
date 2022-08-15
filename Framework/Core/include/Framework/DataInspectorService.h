#ifndef O2_DATAINSPECTORSERVICE_H
#define O2_DATAINSPECTORSERVICE_H

#include <fairlogger/Logger.h>
#include "DISocket.hpp"
#include "Framework/RoutingIndices.h"

namespace o2::framework
{
class DataInspectorService {
 public:
  DataInspectorService(const std::string& deviceName) : DataInspectorService(deviceName, ChannelIndex{-1}) {}
  DataInspectorService(const std::string& deviceName, ChannelIndex dataInspectorChannelIndex);
  ~DataInspectorService();

  void receive();
  void send(const DIMessage& message);
  bool isInspected() { return _isInspected; }
  ChannelIndex getDataInspectorChannelIndex() { return dataInspectorChannelIndex; }

 private:
  void handleMessage(DIMessage& msg);

  const std::string deviceName;
  bool _isInspected = false;
  DISocket socket;

  ChannelIndex dataInspectorChannelIndex;
};
}

#endif //O2_DATAINSPECTORSERVICE_H
