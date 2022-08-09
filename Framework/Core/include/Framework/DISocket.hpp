#ifndef O2_DISOCKET_HPP
#define O2_DISOCKET_HPP

#include <cstring>
#include "boost/asio.hpp"
#include <sstream>

static void fromLE(const char* le, uint64_t* n)
{
  *n = ((uint64_t) ((uint8_t) le[7]) << 56);
  *n += ((uint64_t) ((uint8_t) le[6]) << 48);
  *n += ((uint64_t) ((uint8_t) le[5]) << 40);
  *n += ((uint64_t) ((uint8_t) le[4]) << 32);
  *n += ((uint64_t) ((uint8_t) le[3]) << 24);
  *n += ((uint64_t) ((uint8_t) le[2]) << 16);
  *n += ((uint64_t) ((uint8_t) le[1]) << 8);
  *n += ((uint64_t) (uint8_t) le[0]);
}

static void fromLE(const char* le, uint32_t* n)
{
  *n = ((uint64_t) le[3] << 24)
       + ((uint64_t) le[2] << 16)
       + ((uint64_t) le[1] << 8)
       + (uint64_t) le[0];
}

static void toLE(uint32_t n, char* le)
{
  le[0] = (uint8_t) n;
  le[1] = (uint8_t) (n >> 8);
  le[2] = (uint8_t) (n >> 16);
  le[3] = (uint8_t) (n >> 24);
}

static void toLE(uint64_t n, char* le)
{
  le[0] = (uint8_t) n;
  le[1] = (uint8_t) (n >> 8);
  le[2] = (uint8_t) (n >> 16);
  le[3] = (uint8_t) (n >> 24);
  le[4] = (uint8_t) (n >> 32);
  le[5] = (uint8_t) (n >> 40);
  le[6] = (uint8_t) (n >> 48);
  le[7] = (uint8_t) (n >> 56);
}

struct DIMessage {
  struct Header {
    enum class Type : uint32_t {
      DATA = 1,
      REGISTER_DEVICE = 2,
      INSPECT_ON = 3,
      INSPECT_OFF = 4
    };

    Header(Type type, uint64_t payloadSize) : type(type), payloadSize(payloadSize) {}

    Type type;
    uint64_t payloadSize;
  };

  DIMessage(Header::Type type, const std::string& payload) : header(type, payload.size()), payload(payload) {}

  Header header;
  std::string payload;
};

class DISocket {
 public:
  DISocket(std::unique_ptr<boost::asio::ip::tcp::socket> socket) : socket(std::move(socket)) {}
  DISocket(DISocket&& diSocket)  noexcept : socket(std::move(diSocket.socket)) {}

  DISocket operator=(const DISocket& diSocket) = delete;

  static DISocket connect(const std::string& address, int port)
  {
    static boost::asio::io_context ioContext{};

    auto socket = std::make_unique<boost::asio::ip::tcp::socket>(ioContext);
    socket->connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(address), port));
    return DISocket{std::move(socket)};
  }

  void send(const DIMessage& message)
  {
    char header[12];
    toLE((uint32_t) message.header.type, header);
    toLE(message.header.payloadSize, header+4);

    socket->send(boost::asio::buffer(header, 12));
    socket->send(boost::asio::buffer(message.payload, message.header.payloadSize));
  }

  DIMessage receive()
  {
    //read header
    char header[12];
    socket->read_some(boost::asio::buffer(header, 12));

    //decode header
    uint64_t payloadSize;
    uint32_t type;
    fromLE(header, &type);
    fromLE(header+4, &payloadSize);

    //read payload
    char* payload = new char[payloadSize];
    uint64_t read = 0;
    while(read < payloadSize) {
      read += socket->read_some(boost::asio::buffer(payload + read, payloadSize - read));
    }

    return DIMessage{static_cast<DIMessage::Header::Type>(type), payload};
  }

  bool isReadyToReceive()
  {
    return socket->available() > 12;
  }

  void close()
  {
    socket->close();
  }

 private:
  std::unique_ptr<boost::asio::ip::tcp::socket> socket;
};

class DIAcceptor {
 public:
  DIAcceptor(int port) : acceptor(ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {}

  void start(const std::function<void(DISocket&)>& handleConnection)
  {
    while(running) {
      auto asioSocket = std::make_unique<boost::asio::ip::tcp::socket>(acceptor.accept());
      std::thread{
        [&handleConnection](std::unique_ptr<boost::asio::ip::tcp::socket> asioSocket) -> void{
          DISocket socket(std::move(asioSocket));
          handleConnection(socket);
        }, std::move(asioSocket)}.detach();
    }
  }

  void stop()
  {
    running = false;
  }

 private:
  bool running = true;

  boost::asio::io_context ioContext;
  boost::asio::ip::tcp::acceptor acceptor;
};

#endif //O2_DISOCKET_HPP
