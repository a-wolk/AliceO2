#ifndef O2_DATAINSPECTORSERVICE_H
#define O2_DATAINSPECTORSERVICE_H

#include <fairlogger/Logger.h>
#include "DISocket.hpp"
#include "Framework/RoutingIndices.h"

namespace o2::framework
{
class DataInspectorService {
 public:
  DataInspectorService();

  void receive();
  bool isInspected() { return _isInspected; }
  ChannelIndex getDataInspectorChannel() { return dataInspectorChannelIndex; }
  bool isDataInspectorChannelSet() { return isDataInspectorChannelIndexSet; }
  void setDataInspectorChannel(ChannelIndex dataInspectorChannelIndex) {
    this->dataInspectorChannelIndex = dataInspectorChannelIndex;
    this->isDataInspectorChannelIndexSet = true;
  }

 private:
  void handleMessage(DIMessage& msg);

  const std::string deviceName;
  bool _isInspected = true;
  DISocket socket;

  bool isDataInspectorChannelIndexSet = false;
  ChannelIndex dataInspectorChannelIndex;
};
}

#endif //O2_DATAINSPECTORSERVICE_H
