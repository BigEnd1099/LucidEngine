#include "network/Netcode/ReplicationGraph.hpp"
namespace engine::network {
    void ReplicationGraph::prioritizeEntities(ecs::Registry& registry, const glm::vec3& observerPos) {}
    std::vector<uint8_t> ReplicationGraph::deltaCompressState() { return {}; }
}
