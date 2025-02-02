#include <Analyzer/Passes/OrderByLimitByDuplicateEliminationPass.h>

#include <Analyzer/InDepthQueryTreeVisitor.h>
#include <Analyzer/QueryNode.h>
#include <Analyzer/SortNode.h>

namespace DB
{

namespace
{

struct QueryTreeNodeWithHash
{
    explicit QueryTreeNodeWithHash(const IQueryTreeNode * node_)
        : node(node_)
        , hash(node->getTreeHash().first)
    {}

    const IQueryTreeNode * node = nullptr;
    size_t hash = 0;
};

struct QueryTreeNodeWithHashHash
{
    size_t operator()(const QueryTreeNodeWithHash & node_with_hash) const
    {
        return node_with_hash.hash;
    }
};

struct QueryTreeNodeWithHashEqualTo
{
    bool operator()(const QueryTreeNodeWithHash & lhs_node, const QueryTreeNodeWithHash & rhs_node) const
    {
        return lhs_node.hash == rhs_node.hash && lhs_node.node->isEqual(*rhs_node.node);
    }
};

using QueryTreeNodeWithHashSet = std::unordered_set<QueryTreeNodeWithHash, QueryTreeNodeWithHashHash, QueryTreeNodeWithHashEqualTo>;

class OrderByLimitByDuplicateEliminationVisitor : public InDepthQueryTreeVisitor<OrderByLimitByDuplicateEliminationVisitor>
{
public:
    void visitImpl(QueryTreeNodePtr & node)
    {
        auto * query_node = node->as<QueryNode>();
        if (!query_node)
            return;

        if (query_node->hasOrderBy())
        {
            QueryTreeNodes result_nodes;

            auto & query_order_by_nodes = query_node->getOrderBy().getNodes();

            for (auto & sort_node : query_order_by_nodes)
            {
                auto & sort_node_typed = sort_node->as<SortNode &>();

                /// Skip elements with WITH FILL
                if (sort_node_typed.withFill())
                {
                    result_nodes.push_back(sort_node);
                    continue;
                }

                auto [_, inserted] = unique_expressions_nodes_set.emplace(sort_node_typed.getExpression().get());
                if (inserted)
                    result_nodes.push_back(sort_node);
            }

            query_order_by_nodes = std::move(result_nodes);
        }

        unique_expressions_nodes_set.clear();

        if (query_node->hasLimitBy())
        {
            QueryTreeNodes result_nodes;

            auto & query_limit_by_nodes = query_node->getLimitBy().getNodes();

            for (auto & limit_by_node : query_limit_by_nodes)
            {
                auto [_, inserted] = unique_expressions_nodes_set.emplace(limit_by_node.get());
                if (inserted)
                    result_nodes.push_back(limit_by_node);
            }

            query_limit_by_nodes = std::move(result_nodes);
        }
    }

private:
    QueryTreeNodeWithHashSet unique_expressions_nodes_set;
};

}

void OrderByLimitByDuplicateEliminationPass::run(QueryTreeNodePtr query_tree_node, ContextPtr)
{
    OrderByLimitByDuplicateEliminationVisitor visitor;
    visitor.visit(query_tree_node);
}

}

