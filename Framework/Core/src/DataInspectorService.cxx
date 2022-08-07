#include "Framework/DataInspectorService.h"

struct DIPacket {
  struct Header {
    static const uint32_t HEADER_SIZE = 12;

    char type[4];
    char payloadSize[8];
  };
};

bool PullSocket::isReadyToReceive()
{
  return s.available() > DIPacket::Header::HEADER_SIZE;
}

DIMessage PullSocket::receive()
{
  DIPacket::Header header{};

  boost::system::error_code error;
  size_t length = s.read_some(boost::asio::buffer(&header, DIPacket::Header::HEADER_SIZE), error);

  uint64_t payloadSize;
  fromLE(header.payloadSize, &payloadSize);
  char* payload = new char[payloadSize];

  uint64_t read = 0;
  while(read < payloadSize) {
    length = s.read_some(boost::asio::buffer(payload + read, payloadSize - read), error);
    read += length;
  }

  DIMessage msg = {
    .payload = std::string{payload, payloadSize}
  };

  uint32_t type;
  fromLE(reinterpret_cast<const char *>(&header.type), &type);
  msg.type = static_cast<DIMessage::Type>(type);

  return msg;
}

namespace o2::framework
{
DataInspectorService::DataInspectorService(const std::string &deviceName) : pullSocket("127.0.0.1"), deviceName(deviceName) {}

void DataInspectorService::receive()
{
  if(pullSocket.isReadyToReceive()) {
    DIMessage msg = pullSocket.receive();
    handleMessage(msg);
  }
}

void DataInspectorService::handleMessage(DIMessage &msg)
{
  switch (msg.type) {
    case DIMessage::Type::INSPECT_ON: {
      LOG(info) << "DIService - INSPECT ON";
      _isInspected = true;
      break;
    }
    case DIMessage::Type::INSPECT_OFF: {
      LOG(info) << "DIService - INSPECT OFF";
      _isInspected = false;
      break;
    }
    default: {
      LOG(info) << "DIService - Wrong msg type: " << reinterpret_cast<uint32_t>(msg.type);
    }
  }
}
}