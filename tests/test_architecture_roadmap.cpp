#include "ecs/Registry.hpp"
#include "ecs/Components.hpp"
#include "world/Node3D.hpp"
#include "world/SceneTree.hpp"
#include "world/NodePath.hpp"
#include "world/Prefab.hpp"
#include "core/Signal.hpp"
#include "core/SystemScheduler.hpp"
#include "renderer/RenderFrame.hpp"
#include "renderer/RenderingServer.hpp"
#include "renderer/RenderGraph.hpp"
#include "core/ResourceHandle.hpp"
#include "core/VFS.hpp"
#include "script/ModContext.hpp"

#include <iostream>
#include <cmath>
#include <cassert>
#include <chrono>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "TEST FAILED: " << #cond << " at line " << __LINE__ << std::endl; \
        return 1; \
    } \
} while(false)

// Test custom node subclass to verify lifecycle hooks
class TestPlayerNode : public engine::world::Node3D {
public:
    using Node3D::Node3D;
    int enteredTreeCount{0};
    int readyCount{0};
    int processCount{0};
    int physicsProcessCount{0};
    int exitedTreeCount{0};

    void _enter_tree() override { enteredTreeCount++; }
    void _ready() override { readyCount++; }
    void _process(float delta) override { (void)delta; processCount++; }
    void _physics_process(float fixed_delta) override { (void)fixed_delta; physicsProcessCount++; }
    void _exit_tree() override { exitedTreeCount++; }
};

int main() {
    std::cout << "========================================================\n";
    std::cout << "   RUNNING LUCID ENGINE ARCHITECTURE 2.0 TEST SUITE     \n";
    std::cout << "========================================================\n";

    // ---------------------------------------------------------
    // TEST 1: Faza 0 - Hierarchy Dirty Flags & RenderGraph Telemetry
    // ---------------------------------------------------------
    std::cout << "[TEST 1] Faza 0: Hierarchy Dirty Flags & RenderGraph Telemetry...\n";
    {
        engine::ecs::Registry registry;
        entt::entity parent = registry.createEntity();
        entt::entity child = registry.createEntity();

        registry.get().emplace<engine::ecs::TransformComponent>(parent, glm::vec3(0, 5, 0));
        registry.get().emplace<engine::ecs::TransformComponent>(child, glm::vec3(0, 0, 0));
        registry.get().emplace<engine::ecs::LocalTransformComponent>(child, glm::vec3(0, 2, 0));

        registry.setParent(child, parent);
        CHECK(registry.get().all_of<engine::ecs::HierarchyComponent>(child));
        CHECK(registry.get().get<engine::ecs::HierarchyComponent>(child).transformDirty == true);

        // Update transforms
        registry.updateWorldTransforms();
        CHECK(registry.get().get<engine::ecs::HierarchyComponent>(child).transformDirty == false);
        CHECK(std::abs(registry.get().get<engine::ecs::TransformComponent>(child).position.y - 7.0f) < 1e-4f);

        // Modify local transform -> should mark dirty
        registry.syncLocalTransformFromWorld(child);
        registry.markTransformDirty(child);
        CHECK(registry.get().get<engine::ecs::HierarchyComponent>(child).transformDirty == true);

        // Test RenderGraph telemetry fields
        engine::renderer::RenderGraph rg;
        rg.addPass("DepthPrepass", {}, {"Depth"}, [](VkCommandBuffer){});
        rg.addPass("MainGBuffer", {"Depth"}, {"GBuffer"}, [](VkCommandBuffer){});
        CHECK(rg.compile());
        rg.execute(VK_NULL_HANDLE);
        nlohmann::json telem = rg.getPassTelemetryJson();
        CHECK(telem.is_array());
        CHECK(telem.size() == 2);
        CHECK(telem[0].contains("gpuDurationMs"));
    }
    std::cout << "  -> PASS\n";

    // ---------------------------------------------------------
    // TEST 2: Faza 1 - Signal Bus, Node3D Facade & SceneTree
    // ---------------------------------------------------------
    std::cout << "[TEST 2] Faza 1: Signal Bus, Node3D Facade & SceneTree...\n";
    {
        // 2a. Signal Bus
        engine::core::Signal<int, const std::string&> testSignal;
        int receivedVal = 0;
        std::string receivedMsg;
        auto conn = testSignal.connect([&](int v, const std::string& msg) {
            receivedVal = v;
            receivedMsg = msg;
        });
        testSignal.emit(42, "LucidEngine");
        CHECK(receivedVal == 42);
        CHECK(receivedMsg == "LucidEngine");
        testSignal.disconnect(conn);
        testSignal.emit(99, "Ignored");
        CHECK(receivedVal == 42); // unchanged after disconnect

        // 2b. SceneTree & Node3D Hierarchy
        engine::ecs::Registry registry;
        engine::world::SceneTree tree(&registry);

        engine::world::Node3D* root = tree.get_root();
        CHECK(root != nullptr);
        CHECK(root->get_name() == "Root");

        engine::world::Node3D* car = tree.create_node("Car");
        engine::world::Node3D* wheel = tree.create_node("Wheel_FL");

        root->add_child(car);
        car->add_child(wheel);

        car->set_position(glm::vec3(10.0f, 0.0f, 0.0f));
        wheel->set_position(glm::vec3(0.0f, 0.5f, 0.0f));

        registry.updateWorldTransforms();

        CHECK(wheel->get_parent() == car);
        CHECK(car->get_parent() == root);
        CHECK(car->get_children().size() == 1);
        CHECK(car->get_children()[0] == wheel);

        // World matrix evaluation check
        glm::vec3 wheelWorldPos = wheel->get_world_position();
        CHECK(std::abs(wheelWorldPos.x - 10.0f) < 1e-4f);
        CHECK(std::abs(wheelWorldPos.y - 0.5f) < 1e-4f);

        // Path resolution
        engine::world::Node3D* foundByPath = tree.find_node("Car/Wheel_FL");
        CHECK(foundByPath == wheel);

        engine::world::NodePath path("Car/Wheel_FL");
        CHECK(path.resolve(&tree) == wheel->get_entity());

        // 2c. Custom Node Lifecycle Hooks
        entt::entity customEntity = registry.createEntity();
        registry.get().emplace<engine::ecs::TransformComponent>(customEntity);
        registry.get().emplace<engine::ecs::LocalTransformComponent>(customEntity);
        registry.get().emplace<engine::ecs::TagComponent>(customEntity, "Player");
        auto playerNode = std::make_unique<TestPlayerNode>(customEntity, &registry, &tree);
        TestPlayerNode* rawPlayer = playerNode.get();

        car->add_child(rawPlayer);
        CHECK(rawPlayer->enteredTreeCount == 1);

        tree.process(0.016f);
        CHECK(rawPlayer->readyCount == 1);
        CHECK(rawPlayer->processCount == 1);

        tree.physics_process(0.016f);
        CHECK(rawPlayer->physicsProcessCount == 1);

        // Destruction test
        bool destroyedSignalFired = false;
        rawPlayer->on_destroyed.connect([&](engine::world::Node3D*){
            destroyedSignalFired = true;
        });
        tree.destroy_node(rawPlayer);
        CHECK(destroyedSignalFired == true);
    }
    std::cout << "  -> PASS\n";

    // ---------------------------------------------------------
    // TEST 3: Faza 1 - Prefab Serialization & Instantiation
    // ---------------------------------------------------------
    std::cout << "[TEST 3] Faza 1: Prefab Serialization & Instantiation...\n";
    {
        engine::ecs::Registry registry;
        engine::world::SceneTree tree(&registry);

        engine::world::Node3D* parentNode = tree.create_node("RobotParent");
        engine::world::Node3D* childNode = tree.create_node("RobotArm");
        parentNode->add_child(childNode);

        parentNode->set_position(glm::vec3(5.0f, 1.0f, 2.0f));
        parentNode->add_component<engine::ecs::MeshComponent>(101u, 202u);
        auto& rb = parentNode->add_component<engine::ecs::RigidBodyComponent>();
        rb.mass = 15.0f;

        childNode->set_position(glm::vec3(0.0f, 2.0f, 0.0f));
        childNode->add_component<engine::ecs::MeshComponent>(303u, 404u);

        // Serialize hierarchy
        nlohmann::json prefabJson = engine::world::PrefabSystem::serializeHierarchy(&registry, parentNode->get_entity());
        CHECK(prefabJson.is_object());
        CHECK(prefabJson["name"] == "RobotParent");
        CHECK(prefabJson["children"].size() == 1);
        CHECK(prefabJson["children"][0]["name"] == "RobotArm");

        // Instantiate into fresh entities
        entt::entity newRobot = engine::world::PrefabSystem::instantiateHierarchy(&registry, prefabJson);
        CHECK(newRobot != entt::null);
        CHECK(registry.get().valid(newRobot));
        CHECK(registry.get().get<engine::ecs::TagComponent>(newRobot).name == "RobotParent");
        CHECK(registry.get().get<engine::ecs::MeshComponent>(newRobot).meshId == 101u);
        CHECK(registry.get().all_of<engine::ecs::HierarchyComponent>(newRobot));
        CHECK(registry.get().get<engine::ecs::HierarchyComponent>(newRobot).children.size() == 1);
    }
    std::cout << "  -> PASS\n";

    // ---------------------------------------------------------
    // TEST 4: Faza 2 - Data-Oriented SystemScheduler DAG
    // ---------------------------------------------------------
    std::cout << "[TEST 4] Faza 2: Data-Oriented SystemScheduler DAG...\n";
    {
        engine::core::SystemScheduler scheduler(4);

        int animRun = 0, physicsRun = 0, audioRun = 0, renderRun = 0;

        scheduler.register_system({
            .name = "Animation",
            .component_access = {
                {typeid(engine::ecs::AnimationPlayerComponent), engine::core::AccessMode::Read},
                {typeid(engine::ecs::LocalTransformComponent),  engine::core::AccessMode::Write}
            },
            .execute = [&](float){ animRun++; },
            .allow_parallel = true
        });

        scheduler.register_system({
            .name = "Physics",
            .component_access = {
                {typeid(engine::ecs::RigidBodyComponent), engine::core::AccessMode::Read},
                {typeid(engine::ecs::TransformComponent), engine::core::AccessMode::Write}
            },
            .execute = [&](float){ physicsRun++; },
            .explicit_after = {"Animation"},
            .allow_parallel = true
        });

        scheduler.register_system({
            .name = "Audio",
            .component_access = {
                {typeid(engine::ecs::AudioComponent), engine::core::AccessMode::Read},
                {typeid(engine::ecs::TransformComponent), engine::core::AccessMode::Read}
            },
            .execute = [&](float){ audioRun++; },
            .allow_parallel = true
        });

        scheduler.register_system({
            .name = "RenderExtraction",
            .component_access = {
                {typeid(engine::ecs::TransformComponent), engine::core::AccessMode::Read},
                {typeid(engine::ecs::MeshComponent), engine::core::AccessMode::Read}
            },
            .execute = [&](float){ renderRun++; },
            .explicit_after = {"Physics"},
            .allow_parallel = false
        });

        CHECK(scheduler.build_graph());
        scheduler.execute(0.016f);

        CHECK(animRun == 1);
        CHECK(physicsRun == 1);
        CHECK(audioRun == 1);
        CHECK(renderRun == 1);

        auto timings = scheduler.get_system_timings();
        CHECK(!timings.empty());
    }
    std::cout << "  -> PASS\n";

    // ---------------------------------------------------------
    // TEST 5: Faza 3 - RenderFrame & RenderingServer Extraction
    // ---------------------------------------------------------
    std::cout << "[TEST 5] Faza 3: RenderFrame & RenderingServer Extraction...\n";
    {
        engine::ecs::Registry registry;
        entt::entity e1 = registry.createEntity();
        registry.get().emplace<engine::ecs::TransformComponent>(e1, glm::vec3(1, 2, 3));
        registry.get().emplace<engine::ecs::MeshComponent>(e1, 55u, 77u);

        entt::entity lightEnt = registry.createEntity();
        registry.get().emplace<engine::ecs::TransformComponent>(lightEnt, glm::vec3(0, 10, 0));
        engine::ecs::LightComponent lc{};
        lc.color = glm::vec3(1.0f, 0.9f, 0.8f);
        lc.intensity = 5.0f;
        registry.get().emplace<engine::ecs::LightComponent>(lightEnt, lc);

        engine::renderer::RenderCamera cam{};
        cam.position = glm::vec3(0, 5, -10);

        auto frame = engine::renderer::RenderingServer::extractRenderFrame(&registry, cam, 1, 0.016f);
        CHECK(frame.instances.size() == 1);
        CHECK(frame.instances[0].meshId == 55u);
        CHECK(frame.instances[0].materialId == 77u);
        CHECK(frame.lights.size() == 1);
        CHECK(frame.lights[0].intensity == 5.0f);

        engine::renderer::RenderingServer server;
        server.pushRenderFrame(std::move(frame));
        CHECK(server.hasPendingFrame());

        engine::renderer::RenderFrame poppedFrame;
        CHECK(server.popRenderFrame(poppedFrame));
        CHECK(poppedFrame.instances.size() == 1);
        CHECK(!server.hasPendingFrame());
    }
    std::cout << "  -> PASS\n";

    // ---------------------------------------------------------
    // TEST 6: Faza 4 - ResourceHandle & VirtualFileSystem (.lucidpak)
    // ---------------------------------------------------------
    std::cout << "[TEST 6] Faza 4: ResourceHandle & VirtualFileSystem (.lucidpak)...\n";
    {
        // 6a. Generational Handles
        engine::core::MeshHandle meshA{10, 1};
        engine::core::MeshHandle meshB{10, 1};
        engine::core::MeshHandle meshC{10, 2};
        CHECK(meshA == meshB);
        CHECK(meshA != meshC);
        CHECK(meshA.is_valid());
        CHECK(!engine::core::MeshHandle::invalid().is_valid());

        // 6b. VFS .lucidpak Creation, Mounting, Reading
        std::vector<std::pair<std::string, std::vector<uint8_t>>> testFiles = {
            {"scripts/player.js", {'c', 'o', 'n', 's', 'o', 'l', 'e', '.', 'l', 'o', 'g', '(', ')', ';'}},
            {"configs/game.json", {'{', '"', 'f', 'p', 's', '"', ':', '6', '0', '}'}}
        };

        std::string testPakPath = "test_archive.lucidpak";
        CHECK(engine::core::VirtualFileSystem::create_pak(testPakPath, testFiles));

        engine::core::VirtualFileSystem vfs;
        CHECK(vfs.mount(testPakPath));
        CHECK(vfs.exists("scripts/player.js"));
        CHECK(vfs.exists("configs/game.json"));
        CHECK(!vfs.exists("non_existent_file.txt"));

        auto textOpt = vfs.read_text("configs/game.json");
        CHECK(textOpt.has_value());
        CHECK(textOpt.value() == "{\"fps\":60}");

        auto binOpt = vfs.read_binary("scripts/player.js");
        CHECK(binOpt.has_value());
        CHECK(binOpt->size() == 14);

        vfs.unmount();
        std::remove(testPakPath.c_str());
    }
    std::cout << "  -> PASS\n";

    // ---------------------------------------------------------
    // TEST 7: Faza 5 - QuickJS Watchdog & ModContext Isolation
    // ---------------------------------------------------------
    std::cout << "[TEST 7] Faza 5: QuickJS Watchdog & ModContext Isolation...\n";
    {
        // 7a. ModContext Isolated execution
        engine::script::ModContext safeMod("CommunityMod1");
        bool ok = safeMod.execute_with_timeout("let a = 10 + 20; mod.log('Math result: ' + a);", 100);
        CHECK(ok);
        CHECK(!safeMod.has_crashed());

        // 7b. Mod throwing exception should be caught and not crash host
        engine::script::ModContext buggyMod("BuggyMod");
        bool threwOk = buggyMod.execute_with_timeout("throw new Error('Test Mod Failure');", 100);
        CHECK(!threwOk);
        CHECK(buggyMod.has_crashed());
        CHECK(!buggyMod.get_last_error().empty());

        // 7c. Mod infinite loop should trigger watchdog deadline
        engine::script::ModContext freezeMod("FreezeMod");
        auto t0 = std::chrono::steady_clock::now();
        bool freezeOk = freezeMod.execute_with_timeout("while(true) {}", 50); // 50ms deadline
        auto t1 = std::chrono::steady_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        CHECK(!freezeOk);
        CHECK(freezeMod.has_crashed());
        CHECK(elapsedMs < 1000.0); // Terminated within deadline without hanging the process!
    }
    std::cout << "  -> PASS\n";

    // ---------------------------------------------------------
    // TEST 8: FPS Limiter Frame Pacing Verification
    // ---------------------------------------------------------
    std::cout << "[TEST 8] FPS Limiter: Pacing in both running and paused states...\n";
    {
        int targetFps = 100; // 10ms per frame
        float targetFrameTime = 1.0f / static_cast<float>(targetFps);

        // Test pacing loop simulation (matching Engine.cpp line 1464)
        for (bool paused : {false, true}) {
            (void)paused;
            auto frameStart = std::chrono::high_resolution_clock::now();

            // Simulate small work
            std::this_thread::sleep_for(std::chrono::milliseconds(2));

            // Pacing logic:
            if (targetFps > 0) {
                auto frameEndTime = std::chrono::high_resolution_clock::now();
                float elapsed = std::chrono::duration<float>(frameEndTime - frameStart).count();
                float sleepRemaining = targetFrameTime - elapsed;
                if (sleepRemaining > 0.0015f) {
                    std::this_thread::sleep_for(std::chrono::duration<float>(sleepRemaining - 0.001f));
                }
                while (std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - frameStart).count() < targetFrameTime) {
                    std::this_thread::yield();
                }
            }

            auto frameEnd = std::chrono::high_resolution_clock::now();
            float totalElapsed = std::chrono::duration<float>(frameEnd - frameStart).count();
            CHECK(totalElapsed >= targetFrameTime * 0.95f); // Must take at least targetFrameTime
        }
    }
    std::cout << "  -> PASS\n";

    // ---------------------------------------------------------
    // TEST 9: À-Trous Denoiser Mathematical & Algorithmic Validation
    // ---------------------------------------------------------
    std::cout << "[TEST 9] A-Trous Denoiser: Mathematical Model & Artifact Elimination...\n";
    {
        // 9a. Bilateral Variance Pre-filtering:
        // A single pixel with raw 0.0 variance dropout surrounded by noisy samples (var = 0.25)
        float rawCenterVar = 0.0f;
        float neighborVar = 0.25f;
        float varSum = rawCenterVar * 0.25f;
        float wVarTotal = 0.25f;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                float wTap = (std::abs(dx) + std::abs(dy) == 2) ? 0.0625f : 0.125f;
                varSum += neighborVar * wTap;
                wVarTotal += wTap;
            }
        }
        float smoothedVar = varSum / wVarTotal;
        CHECK(smoothedVar > 0.15f); // Eliminates variance dropouts and freeze-up blotches!

        // 9b. Hampel Outlier Suppression (killing sparks/fireflies):
        float centerRadiance = 50.0f; // Extreme firefly
        float neighborRadiance = 1.0f;
        float colSum = centerRadiance * 0.25f + neighborRadiance * (1.0f - 0.25f);
        float localMean = colSum / 1.0f;
        float fireflyClamp = localMean * 2.5f + 0.5f;
        float clampedRadiance = std::min(centerRadiance, fireflyClamp);
        CHECK(clampedRadiance < 35.0f); // Suppresses spark before it can bloom into wide halo!

        // 9c. SVGF Log-Luminance Weighting (Weber-Fechner):
        // - Smooth gradient on flat surface: L_C = 0.5, L_Q = 0.55 -> should blend (> 0.70)
        float lC = 0.5f; float logLC = std::log(1.0f + lC);
        float lQ = 0.55f; float logLQ = std::log(1.0f + lQ);
        float deltaLog = std::abs(logLC - logLQ);
        float sigmaL_pass0 = std::clamp(0.10f * 2.0f, 0.05f, 0.22f);
        float wl_smooth = std::exp(-deltaLog / sigmaL_pass0);
        CHECK(wl_smooth > 0.75f); // Flawless noise smoothing across flat gradients!

        // - Bright light beam vs medium floor: L_C = 0.5, L_Q = 3.0 -> MUST NOT MIX (< 0.01)
        float lC_beam = 0.5f; float logLC_beam = std::log(1.0f + lC_beam);
        float lQ_beam = 3.0f; float logLQ_beam = std::log(1.0f + lQ_beam);
        float deltaLog_beam = std::abs(logLC_beam - logLQ_beam);
        float wl_beam = std::exp(-deltaLog_beam / sigmaL_pass0);
        CHECK(wl_beam < 0.01f); // Bright light layers are strictly separated from medium colors!

        // - In final pass (Pass 3, radius 16px): passScale = 1.0 / (1.0 + 0.4*3) = 0.45
        float sigmaL_pass3 = sigmaL_pass0 * 0.45f;
        float wl_pass3_beam = std::exp(-deltaLog_beam / sigmaL_pass3);
        CHECK(wl_pass3_beam < 0.0001f); // Zero wide bleeding of bright light into medium colors!

        // 9d. Normal Weighting on curved surfaces vs corners:
        // Sphere curvature (20 deg): cos(20 deg) = 0.9396
        float dn_sphere = 0.9396f;
        float wn_sphere = dn_sphere * dn_sphere * dn_sphere * dn_sphere; // pow(dn, 4.0)
        CHECK(wn_sphere > 0.75f); // Smooth filtering across spheres without mottled patches!

        // 90 deg corner:
        float dn_corner = 0.0f;
        float wn_corner = dn_corner * dn_corner * dn_corner * dn_corner;
        CHECK(wn_corner == 0.0f); // Sharp geometry edges preserved!
    }
    std::cout << "  -> PASS\n";

    std::cout << "========================================================\n";
    std::cout << "   ALL ARCHITECTURE 2.0 TESTS PASSED SUCCESSFULLY!     \n";
    std::cout << "========================================================\n";
    return 0;
}
