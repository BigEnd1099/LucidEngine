#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <glm/glm.hpp>

namespace engine::ai {

    enum class NodeStatus {
        SUCCESS,
        FAILURE,
        RUNNING
    };

    class Blackboard {
    public:
        void setFloat(const std::string& key, float val) { m_floats[key] = val; }
        float getFloat(const std::string& key, float def = 0.0f) const {
            auto it = m_floats.find(key);
            return it != m_floats.end() ? it->second : def;
        }

        void setBool(const std::string& key, bool val) { m_bools[key] = val; }
        bool getBool(const std::string& key, bool def = false) const {
            auto it = m_bools.find(key);
            return it != m_bools.end() ? it->second : def;
        }

        void setVector(const std::string& key, const glm::vec3& val) { m_vectors[key] = val; }
        glm::vec3 getVector(const std::string& key, const glm::vec3& def = glm::vec3(0.0f)) const {
            auto it = m_vectors.find(key);
            return it != m_vectors.end() ? it->second : def;
        }

        void setString(const std::string& key, const std::string& val) { m_strings[key] = val; }
        std::string getString(const std::string& key, const std::string& def = "") const {
            auto it = m_strings.find(key);
            return it != m_strings.end() ? it->second : def;
        }

    private:
        std::unordered_map<std::string, float> m_floats;
        std::unordered_map<std::string, bool> m_bools;
        std::unordered_map<std::string, glm::vec3> m_vectors;
        std::unordered_map<std::string, std::string> m_strings;
    };

    class BTNode {
    public:
        virtual ~BTNode() = default;
        virtual NodeStatus execute(Blackboard& bb, float dt) = 0;
    };

    class SequenceNode : public BTNode {
    public:
        void addChild(std::shared_ptr<BTNode> child) { m_children.push_back(child); }
        virtual NodeStatus execute(Blackboard& bb, float dt) override;
    private:
        std::vector<std::shared_ptr<BTNode>> m_children;
    };

    class SelectorNode : public BTNode {
    public:
        void addChild(std::shared_ptr<BTNode> child) { m_children.push_back(child); }
        virtual NodeStatus execute(Blackboard& bb, float dt) override;
    private:
        std::vector<std::shared_ptr<BTNode>> m_children;
    };

    class ConditionNode : public BTNode {
    public:
        using Predicate = std::function<bool(Blackboard&)>;
        explicit ConditionNode(Predicate pred) : m_predicate(pred) {}
        virtual NodeStatus execute(Blackboard& bb, float dt) override;
    private:
        Predicate m_predicate;
    };

    class ActionNode : public BTNode {
    public:
        using ActionFunc = std::function<NodeStatus(Blackboard&, float)>;
        explicit ActionNode(ActionFunc func) : m_action(func) {}
        virtual NodeStatus execute(Blackboard& bb, float dt) override;
    private:
        ActionFunc m_action;
    };

    class BehaviorTree {
    public:
        explicit BehaviorTree(std::shared_ptr<BTNode> root = nullptr) : m_root(root) {}
        void setRoot(std::shared_ptr<BTNode> root) { m_root = root; }
        NodeStatus tick(float dt);
        Blackboard& getBlackboard() { return m_blackboard; }

    private:
        std::shared_ptr<BTNode> m_root;
        Blackboard m_blackboard;
    };

}
