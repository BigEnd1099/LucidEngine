#include "network/McpServer.hpp"
#include "network/McpKnowledgeBase.hpp"
#include "core/Engine.hpp"
#include "core/Logger.hpp"
#include <iostream>

namespace engine::network::mcp {

    McpServer::McpServer(core::Engine* engine, AgentAccessTier tier)
        : m_engine(engine), m_tier(tier) {
    }

    void McpServer::processStdio() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            try {
                auto req = nlohmann::json::parse(line);
                if (!req.contains("method")) continue;
                
                std::string method = req["method"];
                auto id = req.contains("id") ? req["id"] : nlohmann::json();

                if (method == "initialize") {
                    nlohmann::json initResult = {
                        {"protocolVersion", "2024-11-05"},
                        {"capabilities", {
                            {"tools", nlohmann::json::object()},
                            {"resources", nlohmann::json::object()}
                        }},
                        {"serverInfo", {
                            {"name", "LucidEngine-MCP"},
                            {"version", "2.0.0"}
                        }}
                    };
                    sendResponse(id, initResult);
                } else if (method == "notifications/initialized") {
                    // Client notification - no response required
                } else if (method == "ping") {
                    sendResponse(id, nlohmann::json::object());
                } else if (method == "tools/list") {
                    nlohmann::json tools = McpToolsetRegistry::get().getToolsManifest(m_tier);
                    sendResponse(id, {{"tools", tools}});
                } else if (method == "tools/call") {
                    std::string toolName = req["params"]["name"];
                    auto args = req["params"].contains("arguments") ? req["params"]["arguments"] : nlohmann::json::object();
                    
                    auto invocation = McpToolsetRegistry::get().invoke(m_engine, toolName, args, m_tier);
                    if (invocation.contains("error")) {
                        int code = invocation["error"].value("code", -32000);
                        std::string msg = invocation["error"].value("message", "Error executing tool");
                        sendError(id, code, msg);
                    } else {
                        nlohmann::json res = invocation.value("result", nlohmann::json::object());
                        sendResponse(id, {{"content", {{{"type", "text"}, {"text", res.dump()}}}}});
                    }
                } else if (method == "resources/list") {
                    nlohmann::json resources = nlohmann::json::array({
                        {
                            {"uri", "mcp://lucid/schema"},
                            {"name", "Live Engine Schema & Knowledge Base"},
                            {"description", "Introspekcja komponentow EnTT, uniformow shaderow oraz stanu sceny"},
                            {"mimeType", "application/json"}
                        }
                    });
                    sendResponse(id, {{"resources", resources}});
                } else if (method == "resources/read") {
                    std::string uri = req["params"].value("uri", "");
                    if (uri == "mcp://lucid/schema") {
                        nlohmann::json kb = McpKnowledgeBase::buildKnowledgeGraph(m_engine);
                        sendResponse(id, {
                            {"contents", {{
                                {"uri", uri},
                                {"mimeType", "application/json"},
                                {"text", kb.dump(2)}
                            }}}
                        });
                    } else {
                        sendError(id, -32602, "Resource not found: " + uri);
                    }
                } else {
                    sendError(id, -32601, "Method not found: " + method);
                }
            } catch (const std::exception& e) {
                // Send standard parse/internal error
                sendError(nlohmann::json(), -32700, std::string("Parse error: ") + e.what());
            } catch (...) {
                // Ignore unexpected parse exceptions
            }
        }
    }

    void McpServer::sendResponse(const nlohmann::json& id, const nlohmann::json& result) {
        nlohmann::json res = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
        std::cout << res.dump() << "\n" << std::flush;
    }

    void McpServer::sendError(const nlohmann::json& id, int code, const std::string& message) {
        nlohmann::json err = {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
        std::cout << err.dump() << "\n" << std::flush;
    }

} // namespace engine::network::mcp
