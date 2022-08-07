#ifndef O2_DATAINSPECTORSERVICE_H
#define O2_DATAINSPECTORSERVICE_H

#include <fairlogger/Logger.h>

struct DIMessage {
  enum class Type : uint32_t {
    DATA = 1,
    REGISTER_DEVICE = 2,
    INSPECT_ON = 3,
    INSPECT_OFF = 4
  };

  Type type;
  std::string payload;
};

class PullSocket
{
 public:
  PullSocket(const std::string& address)
  {
    LOG(info) << "PROXY - CONNECT";
    s.connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"), 8081));
    LOG(info) << "PROXY - CONNECTED";
  }

  ~PullSocket() noexcept {}

  bool isReadyToReceive();
  DIMessage receive();

private:
 boost::asio::io_context io_context;
 boost::asio::ip::tcp::socket s{io_context};
};

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
  PullSocket pullSocket;
};
}

#endif //O2_DATAINSPECTORSERVICE_H
