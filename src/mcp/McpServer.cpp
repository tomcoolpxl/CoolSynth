#include "McpServer.h"

#include <atomic>

namespace coolsynth::mcp
{
    namespace
    {
        juce::var object(std::initializer_list<std::pair<const char*, juce::var>> properties)
        {
            auto* o = new juce::DynamicObject();
            for (const auto& [key, value] : properties)
                o->setProperty(key, value);
            return juce::var(o);
        }

        juce::var errorResponse(const juce::var& id, int code, const juce::String& message)
        {
            return object({ { "jsonrpc", "2.0" }, { "id", id }, { "error", object({ { "code", code }, { "message", message } }) } });
        }

        /** The protocol versions this server speaks; the client's own if it is one of them (MCP's version negotiation). */
        juce::String negotiate(const juce::String& asked)
        {
            for (const char* known : { "2025-06-18", "2025-03-26", "2024-11-05" })
                if (asked == known)
                    return asked;
            return Server::latestProtocol;
        }

        /** An Origin from this computer, or none (a client that is not a browser sends none). */
        bool localOrigin(const juce::String& origin)
        {
            if (origin.isEmpty())
                return true;
            const juce::URL url(origin);
            const auto host = url.getDomain().toLowerCase();
            return host == "localhost" || host == "127.0.0.1" || host == "[::1]" || host == "::1";
        }

        juce::String statusText(int status)
        {
            switch (status)
            {
                case 200: return "OK";
                case 202: return "Accepted";
                case 400: return "Bad Request";
                case 403: return "Forbidden";
                case 404: return "Not Found";
                case 405: return "Method Not Allowed";
                case 413: return "Payload Too Large";
                default: return "Error";
            }
        }
    }

    juce::var textResult(const juce::String& text, bool isError)
    {
        juce::Array<juce::var> content;
        content.add(object({ { "type", "text" }, { "text", text } }));
        return object({ { "content", content }, { "isError", isError } });
    }

    juce::var imageResult(const juce::MemoryBlock& png, const juce::String& text)
    {
        juce::MemoryOutputStream base64;
        juce::Base64::convertToBase64(base64, png.getData(), png.getSize());
        juce::Array<juce::var> content;
        content.add(object({ { "type", "image" }, { "data", base64.toString() }, { "mimeType", "image/png" } }));
        content.add(object({ { "type", "text" }, { "text", text } }));
        return object({ { "content", content }, { "isError", false } });
    }

    juce::var objectSchema(std::initializer_list<std::pair<const char*, juce::var>> properties, std::initializer_list<const char*> required)
    {
        auto* props = new juce::DynamicObject();
        for (const auto& [key, schema] : properties)
            props->setProperty(key, schema);
        juce::Array<juce::var> needed;
        for (const auto* key : required)
            needed.add(juce::String(key));
        return object({ { "type", "object" }, { "properties", juce::var(props) }, { "required", needed } });
    }

    juce::var typeSchema(const char* type, const juce::String& description)
    {
        return object({ { "type", juce::String(type) }, { "description", description } });
    }

    Server::Server(juce::String serverName, juce::String serverVersion, std::vector<Tool> toolList, Runner jobRunner)
        : juce::Thread("CoolSynth MCP"), name(std::move(serverName)), version(std::move(serverVersion)), tools(std::move(toolList)),
          runner(std::move(jobRunner))
    {
    }

    Server::~Server()
    {
        stop();
    }

    bool Server::start(int port)
    {
        stop();
        // This computer only (the protocol's advice for a local server): nothing else on the network can reach it.
        if (! listener.createListener(port, "127.0.0.1"))
            return false;
        listeningPort = port;
        startThread(juce::Thread::Priority::low);
        return true;
    }

    void Server::stop()
    {
        signalThreadShouldExit();
        listener.close(); // wakes waitForNextConnection
        stopThread(3000);
        listeningPort = 0;
    }

    void Server::run()
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> client(listener.waitForNextConnection());
            if (client == nullptr)
            {
                if (threadShouldExit() || ! listener.isConnected())
                    break;
                continue;
            }
            serve(*client);
        }
    }

    void Server::serve(juce::StreamingSocket& client)
    {
        // One request each connection, then closed (Connection: close), which HTTP/1.1 clients accept.
        juce::MemoryBlock received;
        constexpr std::size_t headerLimit = 64 * 1024, bodyLimit = 4 * 1024 * 1024;
        int headerEnd = -1;
        char buffer[8192];
        while (headerEnd < 0 && received.getSize() < headerLimit)
        {
            if (client.waitUntilReady(true, 5000) != 1)
                return;
            const int n = client.read(buffer, sizeof(buffer), false);
            if (n <= 0)
                return;
            received.append(buffer, static_cast<std::size_t>(n));
            const auto text = received.toString();
            headerEnd = text.indexOf("\r\n\r\n");
        }
        if (headerEnd < 0)
            return;

        const auto all = received.toString();
        const auto head = all.substring(0, headerEnd);
        auto lines = juce::StringArray::fromLines(head);
        const auto requestLine = juce::StringArray::fromTokens(lines[0], " ", {});
        const auto method = requestLine[0].toUpperCase();
        const auto path = requestLine[1].upToFirstOccurrenceOf("?", false, false);
        juce::String origin;
        int length = 0;
        for (int i = 1; i < lines.size(); ++i)
        {
            const auto key = lines[i].upToFirstOccurrenceOf(":", false, false).trim().toLowerCase();
            const auto value = lines[i].fromFirstOccurrenceOf(":", false, false).trim();
            if (key == "content-length")
                length = value.getIntValue();
            else if (key == "origin")
                origin = value;
        }

        // The body: what came with the head, then the rest by Content-Length, as UTF-8 bytes.
        const auto bodyStart = static_cast<std::size_t>(head.getNumBytesAsUTF8() + 4);
        juce::MemoryBlock body;
        if (received.getSize() > bodyStart)
            body.append(static_cast<const char*>(received.getData()) + bodyStart, received.getSize() - bodyStart);
        HttpAnswer answer;
        if (length < 0 || static_cast<std::size_t>(length) > bodyLimit)
            answer.status = 413;
        else
        {
            while (body.getSize() < static_cast<std::size_t>(length))
            {
                if (client.waitUntilReady(true, 5000) != 1)
                    return;
                const int n = client.read(buffer, static_cast<int>(std::min<std::size_t>(sizeof(buffer), static_cast<std::size_t>(length) - body.getSize())), false);
                if (n <= 0)
                    return;
                body.append(buffer, static_cast<std::size_t>(n));
            }
            answer = handleHttp(method, path, origin, juce::String::fromUTF8(static_cast<const char*>(body.getData()), static_cast<int>(body.getSize())));
        }

        const auto bodyText = answer.body.toStdString();
        juce::String response = "HTTP/1.1 " + juce::String(answer.status) + " " + statusText(answer.status) + "\r\n";
        if (answer.contentType.isNotEmpty())
            response << "Content-Type: " << answer.contentType << "\r\n";
        if (answer.allow.isNotEmpty())
            response << "Allow: " << answer.allow << "\r\n";
        response << "Content-Length: " << juce::String(static_cast<juce::int64>(bodyText.size())) << "\r\n"
                 << "Connection: close\r\n\r\n";
        const auto headText = response.toStdString();
        client.write(headText.data(), static_cast<int>(headText.size()));
        if (! bodyText.empty())
            client.write(bodyText.data(), static_cast<int>(bodyText.size()));
        client.close();
    }

    Server::HttpAnswer Server::handleHttp(const juce::String& method, const juce::String& path, const juce::String& origin,
                                          const juce::String& body)
    {
        HttpAnswer answer;
        if (! localOrigin(origin))
        {
            answer.status = 403; // a web page's request: DNS rebinding, refused
            return answer;
        }
        if (path != "/mcp")
        {
            answer.status = 404;
            return answer;
        }
        if (method != "POST")
        {
            answer.status = 405; // no stream from the server (GET), no session to end (DELETE)
            answer.allow = "POST";
            return answer;
        }
        const auto message = juce::JSON::parse(body);
        if (! message.isObject() && ! message.isArray())
        {
            answer.status = 400;
            answer.contentType = "application/json";
            answer.body = juce::JSON::toString(errorResponse({}, -32700, "Parse error"), true);
            return answer;
        }
        const auto reply = handleMessage(message);
        if (reply.isVoid())
        {
            answer.status = 202; // a notification or a response: accepted, no body
            return answer;
        }
        answer.contentType = "application/json";
        answer.body = juce::JSON::toString(reply, true);
        return answer;
    }

    juce::var Server::handleMessage(const juce::var& message)
    {
        if (const auto* batch = message.getArray())
        {
            juce::Array<juce::var> replies;
            for (const auto& item : *batch)
                if (const auto reply = handleMessage(item); ! reply.isVoid())
                    replies.add(reply);
            return replies.isEmpty() ? juce::var() : juce::var(replies);
        }
        const auto method = message["method"].toString();
        const bool isRequest = message.hasProperty("id") && ! message["id"].isVoid();
        if (method.isEmpty() || ! isRequest)
            return {}; // a response from the client, or a notification (notifications/initialized): nothing to say
        bool failed = false;
        int code = 0;
        juce::String text;
        const auto result = handleRequest(method, message["params"], failed, code, text);
        if (failed)
            return errorResponse(message["id"], code, text);
        return object({ { "jsonrpc", "2.0" }, { "id", message["id"] }, { "result", result } });
    }

    juce::var Server::handleRequest(const juce::String& method, const juce::var& params, bool& failed, int& code, juce::String& message)
    {
        if (method == "initialize")
            return object({ { "protocolVersion", negotiate(params["protocolVersion"].toString()) },
                            { "capabilities", object({ { "tools", object({ { "listChanged", false } }) } }) },
                            { "serverInfo", object({ { "name", name }, { "version", version } }) },
                            { "instructions", "The running CoolSynth app: read and set its sound, play notes, see its window, "
                                              "read its flight log. Values are read and written as the screen shows them." } });
        if (method == "ping")
            return object({});
        if (method == "tools/list")
        {
            juce::Array<juce::var> list;
            for (const auto& tool : tools)
                list.add(object({ { "name", tool.name }, { "description", tool.description }, { "inputSchema", tool.inputSchema } }));
            return object({ { "tools", list } });
        }
        if (method == "tools/call")
        {
            bool found = false;
            auto result = callTool(params["name"].toString(), params["arguments"], found);
            if (! found)
            {
                failed = true;
                code = -32602;
                message = "Unknown tool: " + params["name"].toString();
            }
            return result;
        }
        failed = true;
        code = -32601;
        message = "Method not found: " + method;
        return {};
    }

    juce::var Server::callTool(const juce::String& toolName, const juce::var& arguments, bool& found)
    {
        for (const auto& tool : tools)
        {
            if (tool.name != toolName)
                continue;
            found = true;
            const auto args = arguments.isObject() ? arguments : juce::var(new juce::DynamicObject());
            if (tool.ownThread || runner == nullptr)
                return tool.call(args);
            // The job owns what it touches: if the runner gives up waiting, a late job writes into its own copy.
            struct Pending
            {
                juce::var result;
                std::atomic<bool> done { false };
            };
            auto pending = std::make_shared<Pending>();
            runner([pending, call = tool.call, args]
            {
                pending->result = call(args);
                pending->done.store(true, std::memory_order_release);
            });
            if (! pending->done.load(std::memory_order_acquire))
                return textResult("The app did not answer in time.", true);
            return pending->result;
        }
        return {};
    }
}
