#include "ai/BehaviorTree.hpp"

namespace engine::ai {

    NodeStatus SequenceNode::execute(Blackboard& bb, float dt) {
        for (auto& child : m_children) {
            NodeStatus status = child->execute(bb, dt);
            if (status != NodeStatus::SUCCESS) {
                return status; // Return FAILURE or RUNNING immediately
            }
        }
        return NodeStatus::SUCCESS;
    }

    NodeStatus SelectorNode::execute(Blackboard& bb, float dt) {
        for (auto& child : m_children) {
            NodeStatus status = child->execute(bb, dt);
            if (status != NodeStatus::FAILURE) {
                return status; // Return SUCCESS or RUNNING immediately
            }
        }
        return NodeStatus::FAILURE;
    }

    NodeStatus ConditionNode::execute(Blackboard& bb, float) {
        if (!m_predicate) return NodeStatus::FAILURE;
        return m_predicate(bb) ? NodeStatus::SUCCESS : NodeStatus::FAILURE;
    }

    NodeStatus ActionNode::execute(Blackboard& bb, float dt) {
        if (!m_action) return NodeStatus::FAILURE;
        return m_action(bb, dt);
    }

    NodeStatus BehaviorTree::tick(float dt) {
        if (!m_root) return NodeStatus::FAILURE;
        return m_root->execute(m_blackboard, dt);
    }

}
