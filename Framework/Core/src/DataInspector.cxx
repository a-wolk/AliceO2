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

using namespace rapidjson;

constexpr char PROXY_ADDRESS[]{"ipc:///tmp/proxy"};

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

  void send(const std::string& message)
  {
    LOG(info) << "PROXY - SEND";
    boost::asio::write(s, boost::asio::buffer(message, message.size()));
  }

private:
    boost::asio::io_context io_context;
    boost::asio::ip::tcp::socket s{io_context};
};

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
  void sendToProxy(std::shared_ptr <PushSocket> socket, ProcessingContext &context)
  {
    for (const DataRef &ref: context.inputs()) {
      socket->send(std::string{ref.header});
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

    auto pusher = std::make_shared<PushSocket>(PROXY_ADDRESS);
    dataInspector.algorithm = AlgorithmSpec{
      [p{pusher}](ProcessingContext &context) mutable {
        sendToProxy(p, context);
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