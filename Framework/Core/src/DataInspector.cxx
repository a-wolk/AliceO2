#include "Framework/DataInspector.h"
#include "Framework/DataProcessorSpec.h"
#include "Framework/DeviceSpec.h"
#include "Framework/OutputObjHeader.h"
#include "Framework/RawDeviceService.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fcntl.h>
#include <iomanip>
#include <ios>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include "boost/asio.hpp"
#include <TBufferJSON.h>

using namespace rapidjson;

constexpr char PROXY_ADDRESS[]{"ipc:///tmp/proxy"};

struct DIPacket {
  struct Header {
    static const uint32_t HEADER_SIZE = 12;
    enum class Type : uint32_t {
      DATA = 1,
      REGISTER_DEVICE = 2
    };

    char type[4];
    char payloadSize[8];
  };

  Header header;
  char* payload;
};

static void toLE(uint32_t n, char* le) {
  le[0] = (uint8_t) n;
  le[1] = (uint8_t) (n >> 8);
  le[2] = (uint8_t) (n >> 16);
  le[3] = (uint8_t) (n >> 24);
}

static void toLE(uint64_t n, char* le) {
  le[0] = (uint8_t) n;
  le[1] = (uint8_t) (n >> 8);
  le[2] = (uint8_t) (n >> 16);
  le[3] = (uint8_t) (n >> 24);
  le[4] = (uint8_t) (n >> 32);
  le[5] = (uint8_t) (n >> 40);
  le[6] = (uint8_t) (n >> 48);
  le[7] = (uint8_t) (n >> 56);
}

static DIPacket createPacket(DIPacket::Header::Type type, char* payload, uint64_t payloadSize) {
  DIPacket packet{
    .payload = payload
  };

  toLE((uint32_t) type, &(packet.header.type[0]));
  toLE(payloadSize, &(packet.header.payloadSize[0]));

  return packet;
}

class PushSocket
{
 public:
  PushSocket(const std::string& address)
  {
    LOG(info) << "PROXY - CONNECT" << address;
    s.connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"), 8081));
  }

  ~PushSocket() noexcept
  {
    LOG(info) << "PROXY - DISCONNECT";
  }

  void send(const std::string& message, DIPacket::Header::Type payloadType = DIPacket::Header::Type::DATA)
  {
    uint64_t size = message.size();
    LOG(info) << "PROXY - SEND " << size;

    char payload[size];
    std::memcpy(payload, message.c_str(), size);
    auto packet = createPacket(payloadType, payload, size);
    boost::asio::write(s, boost::asio::buffer(&packet.header, DIPacket::Header::HEADER_SIZE));
    boost::asio::write(s, boost::asio::buffer(packet.payload, size));

    LOG(info) << "PROXY - SENT";
  }

private:
    boost::asio::io_context io_context;
    boost::asio::ip::tcp::socket s{io_context};
};

/* Converts the `data` input a string of bytes as two hexadecimal numbers. Adds
   a 128 offset to deal with negative values. */
template <typename T>
static std::string asBytes(const T* data, uint32_t length)
{
    if (length == 0) {
        return "";
    }
    std::stringstream buffer;
    buffer << std::hex << std::setfill('0')
           << std::setw(2) << static_cast<unsigned>(data[0] + 128);
    for (uint32_t i = 1; i < length; i++) {
        buffer << ' ' << std::setw(2) << static_cast<unsigned>(data[i] + 128);
    }
    return buffer.str();
}

/* Replaces (in place) all occurences of `pattern` in `data` with `substitute`.
   `substitute` may be wider than `pattern`. */
static void replaceAll(
  std::string& data,
  const std::string& pattern,
  const std::string& substitute)
{
  std::string::size_type pos = data.find(pattern);
  while (pos != std::string::npos) {
    data.replace(pos, pattern.size(), substitute);
    pos = data.find(pattern, pos + substitute.size());
  }
}

namespace o2::framework
{
  bool isDataInspectorActive(FairMQDevice &device)
  {
    try {
      if (device.fConfig == nullptr) {
        return false;
      }
      return device.fConfig->GetProperty<bool>(INSPECTOR_ACTIVATION_PROPERTY);
    } catch (...) {
      return false;
    }
  }

  FairMQParts copyMessage(FairMQParts &parts)
  {
    FairMQParts partsCopy;
    for (auto &part: parts) {
      FairMQTransportFactory *transport = part->GetTransport();
      FairMQMessagePtr message(transport->CreateMessage());
      message->Copy(*part);
      partsCopy.AddPart(std::move(message));
    }
    return partsCopy;
  }

  void sendCopyToDataInspector(FairMQDevice &device, FairMQParts &parts, unsigned index)
  {
    for (const auto &[name, channels]: device.fChannels) {
      if (name.find("to_DataInspector") != std::string::npos) {
        FairMQParts copy = copyMessage(parts);
        device.Send(copy, name, index);
        return;
      }
    }
  }

  /* Returns the name of an O2 device which is the source of a route in `routes`
  which matches with `matcher`. If no such route exists, return an empty
  string. */
  std::string findSenderByRoute(
    const std::vector <InputRoute> &routes,
    const InputSpec &matcher)
  {
    for (const InputRoute &route: routes) {
      if (route.matcher == matcher) {
        std::string::size_type start = route.sourceChannel.find('_') + 1;
        std::string::size_type end = route.sourceChannel.find('_', start);
        return route.sourceChannel.substr(start, end - start);
      }
    }
    return "";
  }

  /* Callback which transforms each `DataRef` in `context` to a JSON object and
  sends it on the `socket`. The messages are sent separately. */
  static void sendToProxy(std::shared_ptr<PushSocket> socket, ProcessingContext& context)
  {
    DeviceSpec device = context.services().get<RawDeviceService>().spec();
    for (const DataRef& ref : context.inputs()) {
      Document message;
      message.SetObject();
      Document::AllocatorType& alloc = message.GetAllocator();

      std::string sender = findSenderByRoute(device.inputs, *ref.spec);
      message.AddMember("sender", Value(sender.c_str(), alloc), alloc);

      const header::BaseHeader* baseHeader = header::BaseHeader::get(reinterpret_cast<const std::byte*>(ref.header));
      for (; baseHeader != nullptr; baseHeader = baseHeader->next()) {
        if (baseHeader->description == header::DataHeader::sHeaderType) {
          const auto* header = header::get<header::DataHeader*>(baseHeader->data());
          std::string origin = header->dataOrigin.as<std::string>();
          std::string description = header->dataDescription.as<std::string>();
          std::string method = header->payloadSerializationMethod.as<std::string>();
          std::string bytes = asBytes(ref.payload, header->payloadSize);

          message.AddMember("origin", Value(origin.c_str(), alloc), alloc);
          message.AddMember("description", Value(description.c_str(), alloc), alloc);
          message.AddMember("subSpecification", Value(header->subSpecification), alloc);
          message.AddMember("firstTForbit", Value(header->firstTForbit), alloc);
          message.AddMember("tfCounter", Value(header->tfCounter), alloc);
          message.AddMember("runNumber", Value(header->runNumber), alloc);
          message.AddMember("payloadSize", Value(header->payloadSize), alloc);
          message.AddMember("splitPayloadParts", Value(header->splitPayloadParts), alloc);
          message.AddMember("payloadSerialization", Value(method.c_str(), alloc), alloc);
          message.AddMember("payloadSplitIndex", Value(header->splitPayloadIndex), alloc);
          message.AddMember("payloadBytes", Value(bytes.c_str(), alloc), alloc);
          if (header->payloadSerializationMethod == header::gSerializationMethodROOT) {
            std::unique_ptr<TObject> object = DataRefUtils::as<TObject>(ref);
            TString json = TBufferJSON::ToJSON(object.get());
            message.AddMember("payload", Value(json.Data(), alloc), alloc);
          }
        } else if (baseHeader->description == DataProcessingHeader::sHeaderType) {
          const auto* header = header::get<DataProcessingHeader*>(baseHeader->data());
          message.AddMember("startTime", Value(header->startTime), alloc);
          message.AddMember("duration", Value(header->duration), alloc);
          message.AddMember("creationTimer", Value(header->creation), alloc);
        } else if (baseHeader->description == OutputObjHeader::sHeaderType) {
          const auto* header = header::get<OutputObjHeader*>(baseHeader->data());
          message.AddMember("taskHash", Value(header->mTaskHash), alloc);
        }
      }
      StringBuffer buffer;
      Writer<StringBuffer> writer(buffer);
      message.Accept(writer);

      socket->send(std::string{buffer.GetString(), buffer.GetSize()});
    }
  }

  inline bool isNonInternalDevice(const DataProcessorSpec &device)
  {
    return device.name.find("internal") == std::string::npos;
  }

  inline bool isNonEmptyOutput(const OutputSpec &output)
  {
    return !output.binding.value.empty();
  }

  /* Transforms an `OutputSpec` into an `InputSpec`. For each output route,
  creates a new input route with the same values. */
  inline InputSpec asInputSpec(const OutputSpec &output) {
    return std::visit(
      [&output](auto &&matcher) {
        using T = std::decay_t<decltype(matcher)>;
        if constexpr(std::is_same_v < T, ConcreteDataMatcher > )
        {
          return InputSpec{output.binding.value, matcher, output.lifetime};
        } else {
          ConcreteDataMatcher matcher_{matcher.origin, matcher.description, 0};
          return InputSpec{output.binding.value, matcher_, output.lifetime};
        }
      },
      output.matcher);
  }

  void addDataInspector(WorkflowSpec &workflow)
  {
    DataProcessorSpec dataInspector{"DataInspector"};

    dataInspector.algorithm = AlgorithmSpec{[&workflow](InitContext &context) -> AlgorithmSpec::ProcessCallback{
      auto pusher = std::make_shared<PushSocket>(PROXY_ADDRESS);

      //DI: REGISTER DEVICES
      for (const DataProcessorSpec &device: workflow) {
        if (isNonInternalDevice(device) && !isInspectorDevice(device)) {
          pusher->send(device.name, DIPacket::Header::Type::REGISTER_DEVICE);
        }
      }

      return [p{pusher}](ProcessingContext &context) mutable {
        sendToProxy(p, context);
      };
    }};

    for (const DataProcessorSpec &device: workflow) {
      if (isNonInternalDevice(device)) {
        for (const OutputSpec &output: device.outputs) {
          if (isNonEmptyOutput(output)) {
            dataInspector.inputs.emplace_back(asInputSpec(output));
          }
        }
      }
    }

    workflow.emplace_back(std::move(dataInspector));
  }
}