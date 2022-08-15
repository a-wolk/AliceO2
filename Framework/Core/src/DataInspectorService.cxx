#include "Framework/DataInspectorService.h"

namespace o2::framework
{
DataInspectorService::DataInspectorService(const std::string& deviceName, ChannelIndex dataInspectorChannelIndex) : deviceName(deviceName), dataInspectorChannelIndex(dataInspectorChannelIndex), socket(DISocket::connect("127.0.0.1", 8081))
{
  socket.send(DIMessage{DIMessage::Header::Type::DEVICE_ON, deviceName});
}

DataInspectorService::~DataInspectorService()
{
  socket.send(DIMessage{DIMessage::Header::Type::DEVICE_OFF, deviceName});
  socket.close();
}

void DataInspectorService::receive()
{
  if(socket.isReadyToReceive()) {
    DIMessage msg = socket.receive();
    handleMessage(msg);
  }
}

void DataInspectorService::send(const DIMessage& msg)
{
  socket.send(msg);
}

void DataInspectorService::handleMessage(DIMessage &msg)
{
  switch (msg.header.type) {
    case DIMessage::Header::Type::INSPECT_ON: {
      LOG(info) << "DIService - INSPECT ON";
      _isInspected = true;
      break;
    }
    case DIMessage::Header::Type::INSPECT_OFF: {
      LOG(info) << "DIService - INSPECT OFF";
      _isInspected = false;
      break;
    }
    default: {
      LOG(info) << "DIService - Wrong msg type: " << static_cast<uint32_t>(msg.header.type);
    }
  }
}
}