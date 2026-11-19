// The app's MCP server (D225): the protocol's handshake and tools as Claude Code uses them, on a real processor, and a
// real request over a socket on this computer.

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "mcp/CoolSynthTools.h"
#include "interface/InterfaceModel.h"
#include "mcp/McpServer.h"
#include "plugin/SynthAudioProcessor.h"

namespace
{
    juce::var request(int id, const juce::String& method, const juce::var& params = {})
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("jsonrpc", "2.0");
        o->setProperty("id", id);
        o->setProperty("method", method);
        if (! params.isVoid())
            o->setProperty("params", params);
        return juce::var(o);
    }

    juce::var json(const juce::String& text) { return juce::JSON::parse(text); }

    juce::String firstText(const juce::var& response)
    {
        return response["result"]["content"][0]["text"].toString();
    }
}

class McpServerTests final : public juce::UnitTest
{
public:
    McpServerTests() : juce::UnitTest("MCP server", "Tools") {}

    void runTest() override
    {
        auto synth = std::make_unique<SynthAudioProcessor>();
        synth->setPlayConfigDetails(0, 2, 48000.0, 256);
        synth->prepareToPlay(48000.0, 256);
        coolsynth::mcp::Server server("CoolSynth", "test", coolsynth::mcp::coolSynthTools(*synth, nullptr, nullptr), nullptr);

        beginTest("the handshake: the client's protocol version, the tools capability, the server's name");
        {
            const auto reply = server.handleMessage(request(1, "initialize", json(R"({"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t","version":"1"}})")));
            expectEquals(reply["result"]["protocolVersion"].toString(), juce::String("2025-06-18"));
            expect(reply["result"]["capabilities"].hasProperty("tools"));
            expectEquals(reply["result"]["serverInfo"]["name"].toString(), juce::String("CoolSynth"));
            const auto older = server.handleMessage(request(2, "initialize", json(R"({"protocolVersion":"2024-11-05"})")));
            expectEquals(older["result"]["protocolVersion"].toString(), juce::String("2024-11-05"), "an older client gets its own version");
            expect(server.handleMessage(json(R"({"jsonrpc":"2.0","method":"notifications/initialized"})")).isVoid(), "a notification gets no answer");
        }

        beginTest("the tools are listed with their schemas; an unknown method is an error");
        {
            const auto reply = server.handleMessage(request(3, "tools/list"));
            juce::StringArray names;
            if (const auto* list = reply["result"]["tools"].getArray())
                for (const auto& t : *list)
                {
                    names.add(t["name"].toString());
                    expectEquals(t["inputSchema"]["type"].toString(), juce::String("object"));
                }
            for (const char* wanted : { "get_sound", "list_parameters", "set_values", "set_slot_type", "list_presets", "load_preset",
                                        "list_files", "set_file", "play", "panel", "screenshot", "read_log", "read_screen", "click" })
                expect(names.contains(wanted), juce::String("missing tool ") + wanted);
            expectEquals(static_cast<int>(server.handleMessage(request(4, "no/such"))["error"]["code"]), -32601);
            expectEquals(static_cast<int>(server.handleMessage(request(5, "tools/call", json(R"({"name":"nope"})")))["error"]["code"]), -32602);
        }

        beginTest("the tools read and set the processor, values as the screen shows them");
        {
            const auto loaded = server.handleMessage(request(6, "tools/call", json(R"({"name":"load_preset","arguments":{"name":"Basic Saw"}})")));
            expect(firstText(loaded).contains("Basic Saw"), firstText(loaded));
            const auto set = server.handleMessage(request(7, "tools/call", json(R"({"name":"set_values","arguments":{"values":{"pA.play.mode":"Mono"}}})")));
            expect(firstText(set).contains("pA.play.mode = Mono"), firstText(set));
            expectEquals(synth->getValueTreeState().getParameter("pA.play.mode")->getCurrentValueAsText(), juce::String("Mono"));
            const auto typed = server.handleMessage(request(8, "tools/call", json(R"({"name":"set_slot_type","arguments":{"slot":"pA.g2","type":"Noise"}})")));
            expect(firstText(typed).contains("Noise"), firstText(typed));
            const auto sound = json(firstText(server.handleMessage(request(9, "tools/call", json(R"({"name":"get_sound","arguments":{}})")))));
            expectEquals(sound["layers"][0]["playMode"].toString(), juce::String("Mono"));
            expectEquals(sound["layers"][0]["slots"][1]["type"].toString(), juce::String("Noise"));
            expect(sound["layers"][0]["slots"][1]["values"].size() > 0, "a slot's values by name");
            const auto unknown = server.handleMessage(request(10, "tools/call", json(R"({"name":"set_values","arguments":{"values":{"pA.nope":"1"}}})")));
            expect(static_cast<bool>(unknown["result"]["isError"]), "an unknown id is the tool's error");
            const auto parameters = firstText(server.handleMessage(request(11, "tools/call", json(R"({"name":"list_parameters","arguments":{"prefix":"pA.play"}})"))));
            expect(parameters.contains("pA.play.mode | Play Mode | Mono"), parameters);
        }

        beginTest("HTTP: POST only, this computer's pages only, nonsense refused");
        {
            expectEquals(server.handleHttp("GET", "/mcp", {}, {}).status, 405);
            expectEquals(server.handleHttp("POST", "/other", {}, "{}").status, 404);
            expectEquals(server.handleHttp("POST", "/mcp", "https://evil.example", juce::JSON::toString(request(1, "ping"))).status, 403);
            expectEquals(server.handleHttp("POST", "/mcp", "http://localhost:3000", juce::JSON::toString(request(1, "ping"))).status, 200);
            expectEquals(server.handleHttp("POST", "/mcp", {}, "not json").status, 400);
            expectEquals(server.handleHttp("POST", "/mcp", {}, R"({"jsonrpc":"2.0","method":"notifications/initialized"})").status, 202);
        }

        beginTest("a real request over a socket on 127.0.0.1");
        {
            constexpr int port = 47699;
            expect(server.start(port), "the test port is taken");
            juce::StreamingSocket client;
            expect(client.connect("127.0.0.1", port, 2000));
            const auto body = juce::JSON::toString(request(12, "initialize", json(R"({"protocolVersion":"2025-06-18"})"))).toStdString();
            const auto head = ("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nAccept: application/json, text/event-stream\r\n"
                               "Content-Length: " + juce::String(static_cast<juce::int64>(body.size())) + "\r\n\r\n").toStdString();
            client.write(head.data(), static_cast<int>(head.size()));
            client.write(body.data(), static_cast<int>(body.size()));
            juce::MemoryBlock answer;
            char buffer[4096];
            while (client.waitUntilReady(true, 3000) == 1)
            {
                const int n = client.read(buffer, sizeof(buffer), false);
                if (n <= 0)
                    break;
                answer.append(buffer, static_cast<std::size_t>(n));
            }
            const auto text = answer.toString();
            expect(text.startsWith("HTTP/1.1 200 OK"), text.substring(0, 80));
            expect(text.contains("Content-Type: application/json"));
            expectEquals(json(text.fromFirstOccurrenceOf("\r\n\r\n", false, false))["result"]["serverInfo"]["name"].toString(), juce::String("CoolSynth"));
            server.stop();
            expect(! server.listening());
        }

        beginTest("the panel read as text, clicked by name, and one part pictured (D227)");
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(synth->createEditorAndMakeActive());
            juce::Component* shown = editor.get();
            coolsynth::mcp::Server panelServer("CoolSynth", "test",
                                               coolsynth::mcp::coolSynthTools(*synth, [shown] { return shown; }, nullptr), nullptr);
            auto call = [&](const char* arguments) { return panelServer.handleMessage(request(20, "tools/call", json(arguments))); };
            const auto screen = firstText(call(R"({"name":"read_screen","arguments":{}})"));
            for (const char* part : { "MAIN SCREEN", "LAYER SCREEN", "LAYER A", "KNOBS 1-8", "FADERS", "PADS", "MINILAB DISPLAY", "chain.A.<code>", "pad.1 to pad.8", "P1 Ladder" })
                expect(screen.contains(part), juce::String("read_screen lacks ") + part);
            const auto clicked = firstText(call(R"({"name":"click","arguments":{"name":"chain.A.P1"}})"));
            expect(clicked.startsWith("Clicked chain.A.P1"), clicked.substring(0, 120));
            auto& model = synth->getInterfaceModel();
            expect(model.view() == coolsynth::ui::InterfaceModel::View::section && model.section() == 1 && model.slotIndex(1) == 0 && model.part() == 0,
                   "the Shape section on layer A's P1, as a mouse click there does");
            expect(clicked.contains("[SHAPE]"), "and the answer shows it");
            const auto missing = call(R"({"name":"click","arguments":{"name":"nope"}})");
            expect(static_cast<bool>(missing["result"]["isError"]), "an unknown name is the tool's error");
            (void) call(R"({"name":"click","arguments":{"name":"fader.4","value":1.125}})"); // half way up the Master's boost
            expectWithinAbsoluteError(model.masterBoostDb(), 6.0f, 0.15f, "the Master past its top: the boost");
            (void) call(R"({"name":"click","arguments":{"name":"fader.4","value":1.0}})");
            const auto picture = call(R"({"name":"screenshot","arguments":{"area":"layer screen","zoom":2}})");
            expectEquals(picture["result"]["content"][0]["type"].toString(), juce::String("image"));
            expect(picture["result"]["content"][1]["text"].toString().contains("layer screen"));
            const auto wrong = call(R"({"name":"screenshot","arguments":{"area":"the moon"}})");
            expect(static_cast<bool>(wrong["result"]["isError"]), "an unknown part is the tool's error");
        }
    }
};

static McpServerTests mcpServerTests;
