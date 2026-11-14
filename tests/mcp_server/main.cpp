// Drives the MCP server through its real transport, with a fake renderer hook and an ImGui context without any backend.

#include "McpServerInternal.h"

#include "McpJson.h"
#include "McpTransport.h"

#include <imgui.h>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace InGameOverlay;

static int Failures = 0;

#define CHECK(COND) do { if (!(COND)) { printf("FAILED line %d: %s\n", __LINE__, #COND); ++Failures; } } while(0)

class FakeHook_t : public RendererHook_t
{
public:
    std::atomic<int> ScreenshotRequest{ 0 };
    std::atomic<int> HideApp{ -1 };
    std::atomic<int> HideOverlay{ -1 };

    void SetScreenshotCallback(ScreenshotCallback_t, void*) override {}
    bool StartHook(std::function<void()>, ToggleKey[], int, void*) override { return true; }
    void HideAppInputs(bool hide) override { HideApp = hide ? 1 : 0; }
    void HideOverlayInputs(bool hide) override { HideOverlay = hide ? 1 : 0; }
    bool IsStarted() override { return true; }
    char const* GetLibraryName() const override { return "fake \"library\""; }
    RendererHookType_t GetRendererHookType() const override { return RendererHookType_t::Vulkan; }
    uint32_t GetAutoLoadBatchSize() override { return 10; }
    void SetAutoLoadBatchSize(uint32_t) override {}
    RendererResource_t* CreateResource() override { return nullptr; }
    RendererResource_t* CreateAndAttachResource(void const*, uint32_t, uint32_t) override { return nullptr; }
    void TakeScreenshot(ScreenshotType_t type) override { ScreenshotRequest = static_cast<int>(type); }

    // What RendererHookInternal_t does.
    McpServerInternal_t Mcp;
    bool StartMcpServer(std::string const& name = std::string(), McpServerCallbacks_t callbacks = {}) override { return Mcp.Start(this, callbacks, name); }
    void StopMcpServer() override { Mcp.Stop(); }
    // Only for the hooks allocated with new.
    bool StartMcpServerWithTransport(McpServerTransport_t transport, McpServerCallbacks_t callbacks = {}) override { return Mcp.StartWithTransport(this, transport, callbacks); }
    bool PushMcpMessage(char const* message, size_t size) override { return Mcp.PushMessage(message, size); }
    bool AddMcpTool(McpTool_t const& tool) override { return Mcp.AddTool(tool); }
    bool RemoveMcpTool(const char* name) override { return Mcp.RemoveTool(name); }
    void Release() override { StopMcpServer(); delete this; }
};

static std::string ToolText(McpJson_t const& response);

class Client_t
{
    std::unique_ptr<McpConnection_t> _Connection;
    std::string _Pending;

public:
    explicit Client_t(std::unique_ptr<McpConnection_t> connection):
        _Connection(std::move(connection))
    {
    }

    bool Send(std::string const& line)
    {
        std::string message = line + "\n";
        return _Connection->Write(message.data(), message.size());
    }

    // Returns an empty string when the server closed the connection.
    std::string Receive()
    {
        size_t end;
        while ((end = _Pending.find('\n')) == std::string::npos)
        {
            char buffer[65536];
            const size_t count = _Connection->Read(buffer, sizeof(buffer));
            if (count == 0)
                return std::string();

            _Pending.append(buffer, count);
        }

        std::string line = _Pending.substr(0, end);
        _Pending.erase(0, end + 1);
        return line;
    }

    bool Request(std::string const& line, McpJson_t& response)
    {
        if (!Send(line)) return false;
        do { if (!McpJson_t::Parse(Receive(), response)) return false; }
        while (response.Find("id") == nullptr);
        return true;
    }

    McpJson_t CallTool(int id, std::string const& name, std::string const& arguments = "{}")
    {
        static const bool verbose = getenv("MCP_TEST_VERBOSE") != nullptr;
        if (verbose)
            printf("-> %d %s %s\n", id, name.c_str(), arguments.c_str());

        McpJson_t response;
        CHECK(Request("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
            ",\"method\":\"tools/call\",\"params\":{\"name\":\"" + name + "\",\"arguments\":" + arguments + "}}", response));

        auto responseId = response.Find("id");
        CHECK(responseId != nullptr && static_cast<int>(responseId->Number) == id);
        if (verbose)
            printf("<- %s\n", ToolText(response).substr(0, 2000).c_str());
        return response;
    }
};

static bool ToolFailed(McpJson_t const& response)
{
    auto result = response.Find("result");
    auto isError = result != nullptr ? result->Find("isError") : nullptr;
    return isError == nullptr || isError->Bool;
}

static std::string ToolText(McpJson_t const& response)
{
    auto result = response.Find("result");
    auto content = result != nullptr ? result->Find("content") : nullptr;
    if (content == nullptr)
        return std::string();

    for (auto const& entry : content->Array)
    {
        auto text = entry.Find("text");
        if (text != nullptr)
            return text->String;
    }
    return std::string();
}

static int ErrorCode(McpJson_t const& response)
{
    auto error = response.Find("error");
    auto code = error != nullptr ? error->Find("code") : nullptr;
    return code != nullptr ? static_cast<int>(code->Number) : 0;
}

static void TestJson()
{
    McpJson_t value;
    CHECK(McpJson_t::Parse(R"({"a":[1,-2.5,1e2,true,null],"b":"x\n\u00e9\ud83d\ude00"})", value));
    CHECK(value.Find("a")->Array.size() == 5);
    CHECK(value.Find("a")->Array[1].Number == -2.5);
    CHECK(value.Find("a")->Array[2].Number == 100.0);
    CHECK(value.Find("b")->String == "x\n\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(McpJsonQuote("a\"\\\n\x01") == "\"a\\\"\\\\\\n\\u0001\"");

    for (char const* invalid : { "", "{", "[1,]", "{\"a\":}", "01", "1.", "\"\\ud83d\"", "\"\n\"", "{} x", "tru" })
    {
        McpJson_t ignored;
        CHECK(!McpJson_t::Parse(invalid, ignored));
    }

    // Must not blow the stack of the host application.
    McpJson_t ignored;
    CHECK(!McpJson_t::Parse(std::string(100000, '['), ignored));
}

static std::unique_ptr<McpConnection_t> ConnectWithRetry(McpEndpoint_t const& endpoint)
{
    // A Windows pipe is busy for a moment, until the server has released the previous client.
    std::unique_ptr<McpConnection_t> connection;
    for (int i = 0; i < 100 && connection == nullptr; ++i)
    {
        connection = McpConnect(endpoint);
        if (connection == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return connection;
}

static std::atomic<float> LastMouseX{ 0.0f };
static std::atomic<float> LastMouseY{ 0.0f };

// What the application sees of its widgets, published after each frame.
static std::atomic<int> ButtonPresses{ 0 };
static std::atomic<bool> CheckboxValue{ false };
static std::atomic<float> SliderValue{ 0.0f };
static std::atomic<int> DragValue{ 0 };
static std::atomic<int> SliderEdits{ 0 };

// Depth first search in the items and child windows of imgui_tree.
static McpJson_t const* FindTreeItem(McpJson_t const& node, std::string const& label)
{
    auto nodeLabel = node.Find("label");
    if (nodeLabel != nullptr && nodeLabel->String == label && node.Find("items") == nullptr)
        return &node;

    auto text = node.Find("text");
    if (text != nullptr && text->String == label && node.Find("id") == nullptr)
        return &node;

    for (char const* key : { "windows", "items", "children", "child_windows" })
    {
        if (auto list = node.Find(key))
        {
            for (auto const& child : list->Array)
            {
                if (auto found = FindTreeItem(child, label))
                    return found;
            }
        }
    }
    return nullptr;
}

static bool HasState(McpJson_t const* item, char const* state)
{
    auto states = item != nullptr ? item->Find("state") : nullptr;
    if (states == nullptr)
        return false;

    for (auto const& entry : states->Array)
    {
        if (entry.String == state)
            return true;
    }
    return false;
}

static McpJson_t ToolJson(McpJson_t const& response)
{
    McpJson_t json;
    CHECK(McpJson_t::Parse(ToolText(response), json));
    return json;
}

static void TestImGuiTools(Client_t& client)
{
    auto response = client.CallTool(100, "imgui_tree");
    CHECK(!ToolFailed(response));
    McpJson_t tree = ToolJson(response);

    auto window = FindTreeItem(tree, "Test window");
    CHECK(window == nullptr); // Windows are matched by name, not label.
    CHECK(tree.Find("windows") != nullptr && !tree.Find("windows")->Array.empty());

    auto hello = FindTreeItem(tree, "Hello");
    CHECK(hello != nullptr && hello->Find("type")->String == "Text");

    auto button = FindTreeItem(tree, "Press me");
    CHECK(button != nullptr && button->Find("type")->String == "Button");
    CHECK(button != nullptr && button->Find("id")->String.rfind("0x", 0) == 0);

    auto checkbox = FindTreeItem(tree, "Enabled");
    CHECK(checkbox != nullptr && checkbox->Find("type")->String == "Checkbox");
    CHECK(checkbox != nullptr && checkbox->Find("value")->IsBool() && !checkbox->Find("value")->Bool);

    auto slider = FindTreeItem(tree, "Volume");
    CHECK(slider != nullptr && slider->Find("type")->String == "Slider");
    CHECK(slider != nullptr && slider->Find("value")->Number == 0.25);
    CHECK(slider != nullptr && slider->Find("text")->String == "0.250");
    CHECK(slider != nullptr && slider->Find("min")->Number == 0.0 && slider->Find("max")->Number == 1.0);
    CHECK(HasState(slider, "writable"));

    auto input = FindTreeItem(tree, "Name");
    CHECK(input != nullptr && input->Find("type")->String == "InputText");
    CHECK(input != nullptr && input->Find("value")->String == "abc");

    auto secret = FindTreeItem(tree, "Secret");
    CHECK(secret != nullptr && secret->Find("value") == nullptr);

    auto node = FindTreeItem(tree, "Node");
    CHECK(node != nullptr && node->Find("type")->String == "TreeNode" && !HasState(node, "open"));
    CHECK(FindTreeItem(tree, "Inner") == nullptr);

    auto labelText = FindTreeItem(tree, "Value 42");
    CHECK(labelText != nullptr && labelText->Find("label")->String == "Label");

    auto disabled = FindTreeItem(tree, "Disabled button");
    CHECK(HasState(disabled, "disabled"));

    // Window decorations are hidden by default.
    CHECK(ToolText(response).find("\"type\":\"Item\"") == std::string::npos);
    CHECK(ToolText(response).find("ResizeGrip") == std::string::npos);
    response = client.CallTool(131, "imgui_tree", R"({"window":"Closable","include_decorations":true})");
    CHECK(ToolText(response).find("\"CloseButton\"") != std::string::npos);
    CHECK(ToolText(response).find("\"type\":\"Item\"") == std::string::npos);

    response = client.CallTool(101, "imgui_tree", R"({"window":"Nope"})");
    CHECK(ToolFailed(response));
    CHECK(ToolText(response).find("Test window") != std::string::npos);

    response = client.CallTool(102, "imgui_find", R"({"query":"PRESS"})");
    CHECK(!ToolFailed(response));
    McpJson_t found = ToolJson(response);
    CHECK(found.Find("count")->Number == 1);
    CHECK(found.Find("items")->Array[0].Find("path")->String == "Test window/Press me");

    response = client.CallTool(103, "imgui_find", R"({"type":"button","limit":1})");
    found = ToolJson(response);
    CHECK(found.Find("count")->Number >= 2 && found.Find("truncated")->Bool && found.Find("items")->Array.size() == 1);

    // Activation, like the keyboard.
    response = client.CallTool(104, "imgui_item", R"({"label":"Press me","action":"activate"})");
    CHECK(!ToolFailed(response));
    CHECK(ButtonPresses == 1);

    // Through the mouse.
    response = client.CallTool(105, "imgui_item", R"({"label":"Press me"})");
    CHECK(!ToolFailed(response));
    CHECK(ButtonPresses == 2);

    response = client.CallTool(106, "imgui_item", R"({"label":"Disabled button"})");
    CHECK(ToolFailed(response));
    CHECK(ToolFailed(client.CallTool(107, "imgui_item", R"({"label":"Nope"})")));
    CHECK(ToolFailed(client.CallTool(108, "imgui_item", R"({"id":"0x1"})")));
    CHECK(ToolFailed(client.CallTool(109, "imgui_item", R"({"label":"Press me","action":"nope"})")));

    response = client.CallTool(110, "imgui_item", R"({"label":"Enabled","action":"check"})");
    CHECK(!ToolFailed(response));
    McpJson_t action = ToolJson(response);
    CHECK(action.Find("after")->Find("value")->Bool);
    CHECK(HasState(action.Find("after"), "checked"));
    CHECK(CheckboxValue);
    response = client.CallTool(111, "imgui_item", R"({"label":"Enabled","action":"check"})");
    CHECK(ToolJson(response).Find("note") != nullptr);

    response = client.CallTool(112, "imgui_item", R"({"label":"Volume","action":"set_value","value":0.75})");
    CHECK(!ToolFailed(response));
    action = ToolJson(response);
    CHECK(action.Find("written")->Number == 0.75);
    CHECK(SliderValue == 0.75f);
    CHECK(SliderEdits == 1);

    response = client.CallTool(113, "imgui_item", R"({"label":"Volume","action":"set_value","value":5})");
    CHECK(ToolJson(response).Find("written")->Number == 1.0);
    CHECK(SliderValue == 1.0f);

    response = client.CallTool(114, "imgui_item", R"({"label":"Count","action":"set_value","value":42.4})");
    CHECK(ToolJson(response).Find("written")->Number == 42);
    CHECK(DragValue == 42);

    response = client.CallTool(115, "imgui_item", R"({"label":"Enabled","action":"set_value","value":false})");
    CHECK(!ToolFailed(response));
    CHECK(!CheckboxValue);

    CHECK(ToolFailed(client.CallTool(116, "imgui_item", R"({"label":"Press me","action":"set_value","value":1})")));

    response = client.CallTool(117, "imgui_item", R"({"label":"Name","action":"set_text","text":"héllo"})");
    CHECK(!ToolFailed(response));
    CHECK(ToolJson(response).Find("after")->Find("value")->String == "h\xC3\xA9llo");

    // Through the text mode of the slider.
    response = client.CallTool(118, "imgui_item", R"({"label":"Volume","action":"set_text","text":"0.5"})");
    CHECK(!ToolFailed(response));
    CHECK(SliderValue == 0.5f);

    response = client.CallTool(119, "imgui_item", R"({"label":"Node","action":"open"})");
    CHECK(!ToolFailed(response));
    CHECK(HasState(ToolJson(response).Find("after"), "open"));

    tree = ToolJson(client.CallTool(120, "imgui_tree", R"({"window":"Test window"})"));
    node = FindTreeItem(tree, "Node");
    CHECK(node != nullptr && node->Find("children") != nullptr && FindTreeItem(*node, "Inner") != nullptr);

    response = client.CallTool(121, "imgui_find", R"({"query":"inner"})");
    CHECK(ToolJson(response).Find("items")->Array[0].Find("path")->String == "Test window/Node/Inner");

    response = client.CallTool(122, "imgui_window", R"({"name":"Test window","action":"move","x":50,"y":60})");
    CHECK(!ToolFailed(response));
    McpJson_t moved = ToolJson(response);
    auto rect = moved.Find("after")->Find("rect");
    CHECK(rect != nullptr && rect->Array[0].Number == 50.0 && rect->Array[1].Number == 60.0);

    response = client.CallTool(123, "imgui_window", R"({"name":"Test window","action":"collapse"})");
    CHECK(HasState(ToolJson(response).Find("after"), "collapsed"));
    response = client.CallTool(124, "imgui_window", R"({"name":"Test window","action":"expand"})");
    CHECK(!HasState(ToolJson(response).Find("after"), "collapsed"));

    CHECK(ToolFailed(client.CallTool(125, "imgui_window", R"({"name":"Test window","action":"move"})")));
    CHECK(ToolFailed(client.CallTool(126, "imgui_window", R"({"name":"Test window","action":"close"})")));

    response = client.CallTool(127, "imgui_window", R"({"name":"Closable","action":"close"})");
    CHECK(!ToolFailed(response));
    CHECK(HasState(ToolJson(response).Find("after"), "inactive"));

    response = client.CallTool(128, "imgui_state");
    CHECK(!ToolFailed(response));
    McpJson_t state = ToolJson(response);
    CHECK(state.Find("frame_count")->Number > 0);
    CHECK(state.Find("open_popups")->Array.empty());

    response = client.CallTool(129, "imgui_debug_log", R"({"enable":["activeid"],"clear":true})");
    CHECK(!ToolFailed(response));
    bool enabled = false;
    const McpJson_t log = ToolJson(response);
    for (auto const& category : log.Find("enabled")->Array)
        enabled = enabled || category.String == "activeid";
    CHECK(enabled);
    CHECK(ToolFailed(client.CallTool(130, "imgui_debug_log", R"({"enable":["nope"]})")));
}

static void TestProtocol(Client_t& client, FakeHook_t& hook, std::atomic<bool>& overlayVisible, std::atomic<bool>& frameThreadStop)
{
    McpJson_t response;

    CHECK(client.Request(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})", response));
    CHECK(response.Find("result")->Find("protocolVersion")->String == "2025-06-18");
    CHECK(response.Find("result")->Find("serverInfo")->Find("name")->String == "ingame_overlay");

    CHECK(client.Request(R"({"jsonrpc":"2.0","id":"text-id","method":"initialize","params":{"protocolVersion":"1999-01-01"}})", response));
    CHECK(response.Find("id")->String == "text-id");
    CHECK(response.Find("result")->Find("protocolVersion")->String == "2025-11-25");

    // No response to a notification: the next response is the one of the ping.
    CHECK(client.Send(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
    CHECK(client.Request(R"({"jsonrpc":"2.0","id":2,"method":"ping"})", response));
    CHECK(response.Find("id")->Number == 2);
    CHECK(response.Find("result") != nullptr);

    CHECK(client.Request(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})", response));
    auto const& tools = response.Find("result")->Find("tools")->Array;
    CHECK(tools.size() == 16);
    for (auto const& tool : tools)
        CHECK(tool.Find("inputSchema") != nullptr && tool.Find("inputSchema")->IsObject());

    CHECK(client.Request("not json", response));
    CHECK(ErrorCode(response) == -32700);
    CHECK(response.Find("id")->Type == McpJson_t::Type_e::Null);
    CHECK(client.Request("[1]", response));
    CHECK(ErrorCode(response) == -32600);
    CHECK(client.Request(R"({"jsonrpc":"1.0","id":4,"method":"ping"})", response));
    CHECK(ErrorCode(response) == -32600);
    CHECK(client.Request(R"({"jsonrpc":"2.0","id":5,"method":"nope"})", response));
    CHECK(ErrorCode(response) == -32601);
    CHECK(client.Request(R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"nope"}})", response));
    CHECK(ErrorCode(response) == -32602);
    CHECK(client.Request(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":[1]})", response));
    CHECK(ErrorCode(response) == -32602);

    response = client.CallTool(10, "overlay_status");
    CHECK(!ToolFailed(response));
    McpJson_t status;
    CHECK(McpJson_t::Parse(ToolText(response), status));
    CHECK(status.Find("renderer")->String == "Vulkan");
    CHECK(status.Find("library")->String == "fake \"library\"");
    CHECK(status.Find("hook_state")->String == "Ready");
    CHECK(status.Find("frame_running")->Bool);
    CHECK(status.Find("display_width")->Number == 640.0);
    CHECK(status.Find("overlay_visible")->Bool);

    response = client.CallTool(11, "overlay_wait_frames", R"({"count":3})");
    CHECK(!ToolFailed(response));

    response = client.CallTool(12, "overlay_windows");
    CHECK(!ToolFailed(response));
    CHECK(ToolText(response).find("\"name\":\"Test window\"") != std::string::npos);

    CHECK(ToolFailed(client.CallTool(13, "overlay_mouse", R"({"x":1})")));
    CHECK(ToolFailed(client.CallTool(14, "overlay_mouse", R"({"x":1,"y":2,"button":99})")));
    CHECK(ToolFailed(client.CallTool(15, "overlay_mouse", R"({"x":1,"y":2,"action":"nope"})")));
    CHECK(!ToolFailed(client.CallTool(16, "overlay_mouse", R"({"x":12,"y":34,"action":"click"})")));
    CHECK(!ToolFailed(client.CallTool(17, "overlay_wait_frames", R"({"count":5})")));
    CHECK(LastMouseX == 12.0f && LastMouseY == 34.0f);

    CHECK(ToolFailed(client.CallTool(18, "overlay_key", R"({"key":"NotAKey"})")));
    CHECK(!ToolFailed(client.CallTool(19, "overlay_key", R"({"key":"Enter"})")));
    CHECK(!ToolFailed(client.CallTool(20, "overlay_text", R"({"text":"h\u00e9llo"})")));
    CHECK(ToolFailed(client.CallTool(21, "overlay_text")));

    CHECK(!ToolFailed(client.CallTool(22, "overlay_input_policy", R"({"hide_from_app":true})")));
    CHECK(hook.HideApp == 1 && hook.HideOverlay == -1);
    CHECK(!ToolFailed(client.CallTool(23, "overlay_input_policy", R"({"hide_from_overlay":false})")));
    CHECK(hook.HideApp == 1 && hook.HideOverlay == 0);

    CHECK(ToolFailed(client.CallTool(24, "overlay_set_visible")));
    CHECK(!ToolFailed(client.CallTool(25, "overlay_set_visible", R"({"visible":false})")));
    CHECK(!overlayVisible);

    response = client.CallTool(26, "overlay_screenshot", R"({"include_overlay":false,"max_dimension":16})");
    CHECK(!ToolFailed(response));
    auto const& content = response.Find("result")->Find("content")->Array;
    CHECK(content[0].Find("type")->String == "image");
    CHECK(content[0].Find("mimeType")->String == "image/png");
    // The base64 of the PNG signature.
    CHECK(content[0].Find("data")->String.rfind("iVBORw0KGgo", 0) == 0);
    McpJson_t size;
    CHECK(McpJson_t::Parse(content[1].Find("text")->String, size));
    CHECK(size.Find("width")->Number == 16 && size.Find("height")->Number == 8);
    CHECK(size.Find("source_width")->Number == 32 && size.Find("source_height")->Number == 16);

    TestImGuiTools(client);

    // Without frames, a tool answers instead of blocking forever.
    frameThreadStop = true;
    const auto start = std::chrono::steady_clock::now();
    response = client.CallTool(27, "overlay_mouse", R"({"x":1,"y":2})");
    CHECK(ToolFailed(response));
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(8));
    response = client.CallTool(28, "overlay_status");
    CHECK(!ToolFailed(response));
    CHECK(McpJson_t::Parse(ToolText(response), status));
    CHECK(!status.Find("frame_running")->Bool);
}

static void RunFrames(McpServerInternal_t& server, FakeHook_t& hook, std::atomic<bool>& stop)
{
    // BGRA, like most DirectX swapchains.
    std::vector<uint8_t> pixels(4 * 32 * 16, 0x40);
    ScreenshotCallbackParameter_t screenshot{ 32, 16, 4, 4 * 32, pixels.data(), ScreenshotDataFormat_t::B8G8R8A8 };

    while (!stop)
    {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();

        server.OnFrame();

        static bool checkbox = false;
        static float slider = 0.25f;
        static int drag = 5;
        static char name[64] = "abc";
        static char secret[64] = "password";
        static bool closableOpen = true;

        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400.0f, 400.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Test window");
        ImGui::TextUnformatted("Hello");
        if (ImGui::Button("Press me"))
            ++ButtonPresses;
        ImGui::Checkbox("Enabled", &checkbox);
        if (ImGui::SliderFloat("Volume", &slider, 0.0f, 1.0f))
            ++SliderEdits;
        ImGui::DragInt("Count", &drag, 1.0f, 0, 100);
        ImGui::InputText("Name", name, sizeof(name));
        ImGui::InputText("Secret", secret, sizeof(secret), ImGuiInputTextFlags_Password);
        if (ImGui::TreeNode("Node"))
        {
            ImGui::Button("Inner");
            ImGui::TreePop();
        }
        ImGui::LabelText("Label", "Value %d", 42);
        ImGui::BeginDisabled();
        ImGui::Button("Disabled button");
        ImGui::EndDisabled();
        ImGui::End();

        if (closableOpen)
        {
            ImGui::SetNextWindowPos(ImVec2(420.0f, 10.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Closable", &closableOpen);
            ImGui::TextUnformatted("Close me");
            ImGui::End();
        }
        ImGui::Render();

        CheckboxValue = checkbox;
        SliderValue = slider;
        DragValue = drag;

        LastMouseX = ImGui::GetIO().MousePos.x;
        LastMouseY = ImGui::GetIO().MousePos.y;

        if (hook.ScreenshotRequest.exchange(0) != 0)
            CHECK(server.OnScreenshot(&screenshot));

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// Several overlays in the same process (Steam, Epic, GOG...), told apart by their names.
static void TestNamedServers(McpServerInternal_t& server, FakeHook_t& hook, uint32_t pid)
{
    CHECK(!server.Start(&hook, {}, "bad name"));
    CHECK(!server.Start(&hook, {}, "a/b"));
    CHECK(!server.Start(&hook, {}, std::string(McpMaxEndpointNameLength + 1, 'a')));
    CHECK(McpIsValidEndpointName(std::string(McpMaxEndpointNameLength, 'a')));

    // The others go through RendererHook_t, fed by their hook like a real renderer hook feeds them: one server per hook.
    FakeHook_t* steam = new FakeHook_t();
    FakeHook_t* epic = new FakeHook_t();
    FakeHook_t* duplicate = new FakeHook_t();
    CHECK(!steam->StartMcpServer("bad name"));
    steam->Mcp.OnHookState(OverlayHookState::Ready);
    epic->Mcp.OnHookState(OverlayHookState::Ready);
    CHECK(steam->StartMcpServer("steam"));
    CHECK(!steam->StartMcpServer("steam2"));
    CHECK(epic->StartMcpServer("Epic_Games-1"));
    CHECK(!duplicate->StartMcpServer("steam"));
    duplicate->Release();
    CHECK(server.Start(&hook));
    server.OnHookState(OverlayHookState::Ready);

    int listed = 0;
    for (auto const& found : McpFindServers())
    {
        if (found.Pid == pid && (found.Name.empty() || found.Name == "steam" || found.Name == "Epic_Games-1"))
            ++listed;
    }
#if !defined(__APPLE__)
    CHECK(listed == 3);
#endif

    std::atomic<bool> stop{ false };
    std::thread clientThread([&]()
    {
        int requestId = 200;
        for (char const* name : { "", "steam", "Epic_Games-1" })
        {
            auto connection = ConnectWithRetry(McpEndpoint_t{ pid, name });
            CHECK(connection != nullptr);
            if (connection == nullptr)
                continue;

            // Each endpoint reaches its own server. Those started after the hook got ready know it.
            Client_t client(std::move(connection));
            McpJson_t status;
            CHECK(McpJson_t::Parse(ToolText(client.CallTool(requestId++, "overlay_status")), status));
            CHECK(status.Find("name") != nullptr && status.Find("name")->String == name);
            CHECK(status.Find("hook_state") != nullptr && status.Find("hook_state")->String == "Ready");
            CHECK(status.Find("frame_running") != nullptr && status.Find("frame_running")->Bool);

            if (std::string(name) == "steam")
                CHECK(!ToolFailed(client.CallTool(requestId++, "overlay_screenshot", R"({"max_dimension":16})")));
        }
        CHECK(McpConnect(McpEndpoint_t{ pid, "gog" }) == nullptr);
        stop = true;
    });

    std::vector<uint8_t> pixels(4 * 32 * 16, 0x40);
    ScreenshotCallbackParameter_t screenshot{ 32, 16, 4, 4 * 32, pixels.data(), ScreenshotDataFormat_t::B8G8R8A8 };
    int screenshots = 0;
    while (!stop)
    {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        server.OnFrame();
        steam->Mcp.OnFrame();
        epic->Mcp.OnFrame();
        ImGui::Render();

        if (steam->ScreenshotRequest.exchange(0) != 0)
        {
            CHECK(steam->Mcp.OnScreenshot(&screenshot));
            ++screenshots;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    clientThread.join();
    CHECK(screenshots == 1);
    // Not requested: it belongs to the application.
    CHECK(!steam->Mcp.OnScreenshot(&screenshot));
    CHECK(!epic->Mcp.OnScreenshot(&screenshot));

    server.Stop();

    // StopMcpServer closes the endpoint, and the hook can start another one.
    steam->StopMcpServer();
    steam->StopMcpServer();
    CHECK(McpConnect(McpEndpoint_t{ pid, "steam" }) == nullptr);
    CHECK(steam->StartMcpServer("steam"));
    CHECK(ConnectWithRetry(McpEndpoint_t{ pid, "steam" }) != nullptr);
    steam->Release();
    CHECK(McpConnect(McpEndpoint_t{ pid, "steam" }) == nullptr);

    // Release stops the server.
    CHECK(ConnectWithRetry(McpEndpoint_t{ pid, "Epic_Games-1" }) != nullptr);
    epic->Release();
    CHECK(McpConnect(McpEndpoint_t{ pid, "Epic_Games-1" }) == nullptr);
}

// The application's own protocol: no endpoint, it pushes the client's messages and sends the answers.
static void TestCustomTransport(uint32_t pid)
{
    struct Received_t
    {
        std::mutex Mutex;
        std::condition_variable Condition;
        std::vector<std::string> Messages;

        bool WaitFor(size_t count)
        {
            std::unique_lock<std::mutex> lock(Mutex);
            return Condition.wait_for(lock, std::chrono::seconds(10), [&]() { return Messages.size() >= count; });
        }

        McpJson_t Get(size_t index)
        {
            std::lock_guard<std::mutex> lock(Mutex);
            McpJson_t json;
            CHECK(index < Messages.size() && McpJson_t::Parse(Messages[index], json));
            return json;
        }
    } received;

    McpServerTransport_t transport;
    transport.Send = [](char const* message, size_t size, void* userParameter)
    {
        auto& received = *static_cast<Received_t*>(userParameter);
        // One message, without newline.
        CHECK(size != 0 && message[size - 1] != '\n');
        {
            std::lock_guard<std::mutex> lock(received.Mutex);
            received.Messages.emplace_back(message, size);
        }
        received.Condition.notify_all();
    };
    transport.UserParameter = &received;

    auto push = [](FakeHook_t* hook, std::string const& message) { return hook->PushMcpMessage(message.data(), message.size()); };

    FakeHook_t* hook = new FakeHook_t();
    CHECK(!push(hook, R"({"jsonrpc":"2.0","id":1,"method":"ping"})"));
    CHECK(!hook->StartMcpServerWithTransport(McpServerTransport_t()));
    CHECK(hook->StartMcpServerWithTransport(transport));
    CHECK(!hook->StartMcpServerWithTransport(transport));
    CHECK(!hook->StartMcpServer());
    // No endpoint.
    CHECK(McpConnect(McpEndpoint_t{ pid, "" }) == nullptr);
    CHECK(!hook->PushMcpMessage(nullptr, 0));
    CHECK(!push(hook, std::string(1024 * 1024 + 1, ' ')));
    hook->Mcp.OnHookState(OverlayHookState::Ready);

    std::atomic<bool> stop{ false };
    std::thread clientThread([&]()
    {
        CHECK(push(hook, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})"));
        CHECK(received.WaitFor(1));
        CHECK(received.Get(0).Find("result") != nullptr && received.Get(0).Find("result")->Find("serverInfo") != nullptr);

        // A notification gets no answer: the next message answers the ping.
        CHECK(push(hook, R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
        CHECK(push(hook, R"({"jsonrpc":"2.0","id":"two","method":"ping"})"));
        CHECK(received.WaitFor(2));
        CHECK(received.Get(1).Find("id") != nullptr && received.Get(1).Find("id")->String == "two");

        CHECK(push(hook, "not json"));
        CHECK(received.WaitFor(3));
        CHECK(received.Get(2).Find("error") != nullptr && received.Get(2).Find("error")->Find("code")->Number == -32700);

        // A tool which needs the render thread.
        CHECK(push(hook, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"overlay_status","arguments":{}}})"));
        CHECK(received.WaitFor(4));
        McpJson_t status;
        CHECK(McpJson_t::Parse(ToolText(received.Get(3)), status));
        CHECK(status.Find("frame_running") != nullptr && status.Find("frame_running")->Bool);
        CHECK(status.Find("hook_state") != nullptr && status.Find("hook_state")->String == "Ready");
        stop = true;
    });

    while (!stop)
    {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        hook->Mcp.OnFrame();
        ImGui::Render();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    clientThread.join();

    hook->StopMcpServer();
    CHECK(!push(hook, R"({"jsonrpc":"2.0","id":5,"method":"ping"})"));
    // Then the built-in endpoint can be used again.
    CHECK(hook->StartMcpServer("custom"));
    CHECK(!push(hook, R"({"jsonrpc":"2.0","id":5,"method":"ping"})"));
    hook->Release();
}

struct ApplicationState_t
{
    McpServerInternal_t* Server;
    std::atomic<bool> Entered{false}, Leave{false}, Removed{false};
    std::thread::id RenderThread;
};
static void ApplicationTool(const char* args, size_t size, McpToolResponse_t* response, void* parameter)
{
    auto& state = *static_cast<ApplicationState_t*>(parameter);
    CHECK(std::this_thread::get_id() == state.RenderThread);
    std::string input(args, size);
    if (input.find("block") != std::string::npos)
    {
        state.Entered = true;
        while (!state.Leave) std::this_thread::yield();
        response->SetText("done", 4);
    }
    else if (input.find("self") != std::string::npos)
    {
        CHECK(!state.Server->RemoveTool("app_echo"));
        response->SetText("self", 4);
    }
    else if (input.find("error") != std::string::npos)
        response->SetError("failure", 7);
    else response->SetText(args, size);
}
static void TestApplicationTools(Client_t& client, McpServerInternal_t& server, ApplicationState_t& state)
{
    McpJson_t response;
    CHECK(client.Request(R"({"jsonrpc":"2.0","id":900,"method":"initialize"})", response));
    CHECK(response.Raw.find("listChanged") != std::string::npos);
    CHECK(client.Request(R"({"jsonrpc":"2.0","id":901,"method":"tools/list"})", response));
    CHECK(response.Raw.find("app_echo") != std::string::npos);
    CHECK(ToolText(client.CallTool(902, "app_echo", R"({"hello":1})")) == R"({"hello":1})");
    CHECK(ToolFailed(client.CallTool(903, "app_echo", R"({"error":true})")));
    CHECK(ToolText(client.CallTool(904, "app_echo", R"({"self":true})")) == "self");
    McpTool_t tool{"app_other", "other", "{}", ApplicationTool, &state};
    CHECK(server.AddTool(tool));
    McpJson_t notification;
    CHECK(McpJson_t::Parse(client.Receive(), notification));
    CHECK(notification.Raw.find("notifications/tools/list_changed") != std::string::npos);
    CHECK(server.RemoveTool("app_other"));
    std::thread editor([&]()
    {
        for (int i = 0; i < 100; ++i)
        {
            if (!server.AddTool(tool) || !server.RemoveTool(tool.Name)) std::abort();
        }
    });
    for (int i = 0; i < 20; ++i)
        CHECK(client.Request(R"({"jsonrpc":"2.0","id":907,"method":"tools/list"})", response));
    editor.join();
    std::thread remover([&]()
    {
        while (!state.Entered) std::this_thread::yield();
        state.Removed = server.RemoveTool("app_echo");
    });
    std::thread release([&]()
    {
        while (!state.Entered) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(!state.Removed);
        state.Leave = true;
    });
    CHECK(ToolText(client.CallTool(905, "app_echo", R"({"block":true})")) == "done");
    release.join(); remover.join();
    CHECK(state.Removed);
    CHECK(!server.RemoveTool("app_echo"));
    CHECK(client.Request(R"({"jsonrpc":"2.0","id":906,"method":"tools/list"})", response));
    CHECK(response.Raw.find("app_echo") == std::string::npos);
    CHECK(server.AddTool({"app_echo", "echo", "{}", ApplicationTool, &state}));
}

int main()
{
    // The failures must be seen even if an assertion aborts the process afterwards.
    setvbuf(stdout, nullptr, _IONBF, 0);

    TestJson();

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(640.0f, 480.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();

#if defined(_WIN32)
    const McpEndpoint_t endpoint{ static_cast<uint32_t>(_getpid()), std::string() };
#else
    const McpEndpoint_t endpoint{ static_cast<uint32_t>(getpid()), std::string() };
#endif
    const uint32_t pid = endpoint.Pid;

    FakeHook_t hook;
    std::atomic<bool> overlayVisible{ true };

    McpServerCallbacks_t callbacks;
    callbacks.IsOverlayVisible = [](void* userParameter) { return static_cast<std::atomic<bool>*>(userParameter)->load(); };
    callbacks.SetOverlayVisible = [](bool visible, void* userParameter) { static_cast<std::atomic<bool>*>(userParameter)->store(visible); };
    callbacks.UserParameter = &overlayVisible;

    McpServerInternal_t server;
    ApplicationState_t application{&server};
    application.RenderThread = std::this_thread::get_id();
    McpTool_t tool{"app_echo", "echo", "{}", ApplicationTool, &application};
    CHECK(server.AddTool(tool));
    CHECK(!server.AddTool(tool));
    CHECK(!server.AddTool({"overlay_status", "collision", "{}", ApplicationTool, &application}));
    CHECK(!server.AddTool({"bad name", "bad", "{}", ApplicationTool, &application}));
    CHECK(!server.AddTool({"bad_schema", "bad", "{", ApplicationTool, &application}));
    CHECK(!server.OnScreenshot(nullptr));
    CHECK(McpConnect(endpoint) == nullptr);
    CHECK(server.Start(&hook, callbacks));
    CHECK(!server.Start(&hook, callbacks));
    server.OnHookState(OverlayHookState::Ready);

    bool found = false;
    for (auto const& server_endpoint : McpFindServers())
        found = found || (server_endpoint.Pid == pid && server_endpoint.Name.empty());
#if !defined(__APPLE__)
    CHECK(found);
#endif

    std::atomic<bool> stopFrames{ false };
    std::thread clientThread([&]()
    {
        auto connection = ConnectWithRetry(endpoint);
        CHECK(connection != nullptr);
        if (connection != nullptr)
        {
            Client_t client(std::move(connection));
            TestApplicationTools(client, server, application);
            TestProtocol(client, hook, overlayVisible, stopFrames);
        }
        stopFrames = true;
    });

    RunFrames(server, hook, stopFrames);
    clientThread.join();

    // A message which never ends closes the connection, and the server accepts the next client.
    {
        auto connection = ConnectWithRetry(endpoint);
        CHECK(connection != nullptr);
        if (connection != nullptr)
        {
            // The server may close before having read everything, so this write is allowed to fail.
            Client_t client(std::move(connection));
            client.Send(std::string(1024 * 1024 + 1024, 'x'));
            CHECK(client.Receive().empty());
        }
    }

    {
        auto connection = ConnectWithRetry(endpoint);
        CHECK(connection != nullptr);
        if (connection != nullptr)
        {
            Client_t client(std::move(connection));
            McpJson_t response;
            CHECK(client.Request(R"({"jsonrpc":"2.0","id":1,"method":"ping"})", response));

            // Stop must unblock a server waiting for this idle client, and restarting must work.
            server.Stop();
            CHECK(client.Receive().empty());
        }
    }

    CHECK(McpConnect(endpoint) == nullptr);
    CHECK(server.Start(&hook));
    {
        Client_t client(ConnectWithRetry(endpoint));
        McpJson_t response;
        CHECK(client.Request(R"({"jsonrpc":"2.0","id":999,"method":"tools/list"})", response));
        CHECK(response.Raw.find("app_echo") != std::string::npos);
    }
    server.Stop();
    server.Stop();

    TestNamedServers(server, hook, pid);
    TestCustomTransport(pid);

    ImGui::DestroyContext();

    if (Failures != 0)
    {
        printf("%d check(s) failed\n", Failures);
        return 1;
    }

    printf("OK\n");
    return 0;
}
