#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

/*
    A Model Context Protocol server (D225) inside the app, so Claude can read and set the running CoolSynth while
    guiding a test: Streamable HTTP of MCP 2025-06-18 (modelcontextprotocol.io, "Transports"), on 127.0.0.1 only, for
    one local client. A JSON-RPC message comes by POST to /mcp; a request is answered with one JSON object, a
    notification or a response with 202; GET and DELETE get 405 (no server stream, no session to end). A request whose
    Origin is not this computer gets 403, as the protocol asks against DNS rebinding. No library: no official C++ SDK
    exists, and what one client needs is this small (the user's choice).
*/
namespace coolsynth::mcp
{
    /** One tool: its name, what it does, the JSON Schema of its arguments, and the call, which returns an MCP
        CallToolResult ({ content: [...], isError }). */
    struct Tool
    {
        juce::String name;
        juce::String description;
        juce::var inputSchema;
        std::function<juce::var(const juce::var& arguments)> call;
        bool ownThread = false; // runs on the server's thread and posts its own work (a tool that waits)
    };

    /** A text answer, or an error the client shows as the tool's failure. */
    [[nodiscard]] juce::var textResult(const juce::String& text, bool isError = false);
    /** A picture (PNG) with a line of text. */
    [[nodiscard]] juce::var imageResult(const juce::MemoryBlock& png, const juce::String& text);
    /** A JSON Schema object with these properties ({ name: schema }) and these required. */
    [[nodiscard]] juce::var objectSchema(std::initializer_list<std::pair<const char*, juce::var>> properties,
                                         std::initializer_list<const char*> required = {});
    [[nodiscard]] juce::var typeSchema(const char* type, const juce::String& description);

    class Server final : private juce::Thread
    {
    public:
        /** Runs a job and returns once it is done: the app runs tools on its message thread. */
        using Runner = std::function<void(std::function<void()>)>;

        Server(juce::String serverName, juce::String serverVersion, std::vector<Tool> tools, Runner runner);
        ~Server() override;

        /** Listens on 127.0.0.1:port; false if that port is taken (another CoolSynth has it). */
        bool start(int port);
        void stop();
        [[nodiscard]] bool listening() const noexcept { return isThreadRunning(); }
        [[nodiscard]] int port() const noexcept { return listeningPort; }

        /** One JSON-RPC message (or a batch), as the endpoint takes it: the answer, or void for a notification. */
        [[nodiscard]] juce::var handleMessage(const juce::var& message);

        struct HttpAnswer
        {
            int status = 200;
            juce::String contentType, body, allow;
        };
        /** One HTTP request, without a socket (tests): method, path, the Origin header (or empty), the body. */
        [[nodiscard]] HttpAnswer handleHttp(const juce::String& method, const juce::String& path, const juce::String& origin,
                                            const juce::String& body);

        static constexpr const char* latestProtocol = "2025-06-18";

    private:
        void run() override;
        void serve(juce::StreamingSocket& client);
        [[nodiscard]] juce::var handleRequest(const juce::String& method, const juce::var& params, bool& failed, int& code, juce::String& message);
        [[nodiscard]] juce::var callTool(const juce::String& name, const juce::var& arguments, bool& found);

        const juce::String name, version;
        std::vector<Tool> tools;
        Runner runner;
        juce::StreamingSocket listener;
        int listeningPort = 0;
    };
}
