//
// TM & (c) 2017 Lucasfilm Entertainment Company Ltd. and Lucasfilm Ltd.
// All rights reserved.  See LICENSE.txt for license.
//

#include <MaterialXTest/Catch/catch.hpp>

#include <MaterialXCore/Definition.h>
#include <MaterialXCore/Document.h>

#include <MaterialXFormat/XmlIo.h>

namespace mx = MaterialX;

TEST_CASE("Node", "[node]")
{
    // Create a document.
    mx::DocumentPtr doc = mx::createDocument();

    // Create a node graph with two source nodes.
    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph();
    REQUIRE_THROWS_AS(doc->addNodeGraph(nodeGraph->getName()), mx::Exception);
    mx::NodePtr constant = nodeGraph->addNode("constant");
    mx::NodePtr image = nodeGraph->addNode("image");
    REQUIRE(nodeGraph->getChildrenOfType<mx::Node>().size() == 2);
    REQUIRE(nodeGraph->getChildrenOfType<mx::Node>("constant").size() == 1);
    REQUIRE(nodeGraph->getChildrenOfType<mx::Node>("image").size() == 1);

    // Set constant node color.
    mx::Color3 color(0.1f, 0.2f, 0.3f);
    constant->setParameterValue<mx::Color3>("value", color);
    REQUIRE(constant->getParameterValue("value")->isA<mx::Color3>());
    REQUIRE(constant->getParameterValue("value")->asA<mx::Color3>() == color);

    // Set image node file.
    std::string file("image1.tif");
    image->setParameterValue("file", file);
    REQUIRE(image->getParameterValueString("file") == file);

    // Create connected outputs.
    mx::OutputPtr output1 = nodeGraph->addOutput();
    mx::OutputPtr output2 = nodeGraph->addOutput();
    output1->setConnectedNode(constant);
    output2->setConnectedNode(image);
    REQUIRE(output1->getUpstreamElement() == constant);
    REQUIRE(output2->getUpstreamElement() == image);

    // Define and reference a custom type.
    doc->addTypeDef("spectrum");
    std::string d65("400.0,82.75,500.0,109.35,600.0,90.01,700.0,71.61,800.0,59.45");
    constant->setParameterValue<std::string>("value", d65, "spectrum");
    REQUIRE(constant->getParameter("value")->getType() == "spectrum");
    REQUIRE(constant->getParameter("value")->getValueString() == d65);
    REQUIRE(constant->getParameterValue("value")->isA<std::string>());
    REQUIRE(constant->getParameterValue("value")->asA<std::string>() == d65);

    // Disconnect outputs from sources.
    output1->setConnectedNode(nullptr);
    output2->setConnectedNode(nullptr);
    REQUIRE(output1->getUpstreamElement() == nullptr);
    REQUIRE(output2->getUpstreamElement() == nullptr);

    // Remove nodes and outputs.
    nodeGraph->removeNode(image->getName());
    nodeGraph->removeNode(constant->getName());
    nodeGraph->removeOutput(output1->getName());
    nodeGraph->removeOutput(output2->getName());
    REQUIRE(nodeGraph->getChildren().empty());

    // Remove node graph.
    doc->removeNodeGraph(nodeGraph->getName());
    REQUIRE(doc->getNodeGraphs().empty());
}

TEST_CASE("Flatten", "[nodegraph]")
{
    // Load the example file.
    mx::DocumentPtr doc = mx::createDocument();
    mx::readFromXmlFile(doc, "SubGraphs.mtlx", "documents/Examples;documents/Libraries");

    // Find the example graph.
    mx::NodeGraphPtr graph = doc->getChildOfType<mx::NodeGraph>("subgraph_ex1");
    REQUIRE(graph);

    // Traverse the graph and count nodes.
    int totalNodeCount = 0;
    for (mx::ElementPtr elem : graph->traverseTree())
    {
        if (elem->isA<mx::Node>())
        {
            totalNodeCount++;
        }
    }
    REQUIRE(totalNodeCount == 7);

    // Create a flat version of the graph.
    mx::NodeGraphPtr flatGraph = doc->addNodeGraph();
    flatGraph->copyContentFrom(graph);
    flatGraph->flattenSubgraphs();

    // Traverse the flat graph and count nodes.
    totalNodeCount = 0;
    for (mx::ElementPtr elem : flatGraph->traverseTree())
    {
        if (elem->isA<mx::Node>())
        {
            totalNodeCount++;

            // Make sure it's an atomic node.
            mx::ElementPtr implement = elem->asA<mx::Node>()->getImplementation();
            bool isAtomic = !implement || !implement->isA<mx::NodeGraph>();
            REQUIRE(isAtomic);
        }
    }
    REQUIRE(totalNodeCount == 19);
}

TEST_CASE("Topological sort", "[nodegraph]")
{
    // Create a document.
    mx::DocumentPtr doc = mx::createDocument();

    // Create a node graph with the following structure:
    //
    //   [constant1] [constant2]      [image2]
    //           \   /          \    /
    // [image1] [add1]          [add2]
    //        \  /   \______      |   
    //    [multiply]        \__ [add3]         [noise3d]
    //             \____________  |  ____________/
    //                          [mix]
    //                            |
    //                         [output]
    //
    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph();
    mx::NodePtr image1 = nodeGraph->addNode("image", "image1");
    mx::NodePtr image2 = nodeGraph->addNode("image", "image2");
    mx::NodePtr multiply = nodeGraph->addNode("multiply", "multiply");
    mx::NodePtr constant1 = nodeGraph->addNode("constant", "constant1");
    mx::NodePtr constant2 = nodeGraph->addNode("constant", "constant2");
    mx::NodePtr add1 = nodeGraph->addNode("add", "add1");
    mx::NodePtr add2 = nodeGraph->addNode("add", "add2");
    mx::NodePtr add3 = nodeGraph->addNode("add", "add3");
    mx::NodePtr noise3d = nodeGraph->addNode("noise3d", "noise3d");
    mx::NodePtr mix = nodeGraph->addNode("mix", "mix");
    mx::OutputPtr output = nodeGraph->addOutput("output");
    add1->setConnectedNode("in1", constant1);
    add1->setConnectedNode("in2", constant2);
    add2->setConnectedNode("in1", constant2);
    add2->setConnectedNode("in2", image2);
    add3->setConnectedNode("in1", add1);
    add3->setConnectedNode("in2", add2);
    multiply->setConnectedNode("in1", image1);
    multiply->setConnectedNode("in2", add1);
    mix->setConnectedNode("fg", multiply);
    mix->setConnectedNode("bg", add3);
    mix->setConnectedNode("mask", noise3d);
    output->setConnectedNode(mix);

    // Validate the document.
    REQUIRE(doc->validate());

    // Blessed solution to the topological sort
    const std::vector<std::string> blessedElemOrder =
    {
        "output",
        "mix",
        "multiply",
        "add3",
        "noise3d",
        "image1",
        "add1",
        "add2",
        "constant1",
        "constant2",
        "image2"
    };

    // Create a topological order and test it agains the blessed solution
    std::vector<mx::ElementPtr> elemOrder = nodeGraph->topologicalSort();
    REQUIRE(elemOrder.size() == blessedElemOrder.size());
    for (size_t i = 0; i < elemOrder.size(); ++i)
    {
        REQUIRE(elemOrder[i]->getName() == blessedElemOrder[i]);
    }
}

TEST_CASE("New nodegraph from output", "[nodegraph]")
{
    // Create a document.
    mx::DocumentPtr doc = mx::createDocument();

    // Create a node graph with the following structure:
    //
    //   [constant1] [constant2]      [image2]
    //           \   /          \    /
    // [image1] [add1]          [add2]
    //        \  /   \______      |   
    //   [multiply1]        \__ [add3]         [noise3d]            [constant3]
    //             \____________  |  ____________/    \                /
    //                          [mix]                  \_ [multiply2]_/
    //                            |                           |
    //                          [out1]                      [out2]
    //
    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph();
    mx::NodePtr image1 = nodeGraph->addNode("image", "image1");
    mx::NodePtr image2 = nodeGraph->addNode("image", "image2");
    mx::NodePtr multiply1 = nodeGraph->addNode("multiply", "multiply1");
    mx::NodePtr multiply2 = nodeGraph->addNode("multiply", "multiply2");
    mx::NodePtr constant1 = nodeGraph->addNode("constant", "constant1");
    mx::NodePtr constant2 = nodeGraph->addNode("constant", "constant2");
    mx::NodePtr constant3 = nodeGraph->addNode("constant", "constant3");
    mx::NodePtr add1 = nodeGraph->addNode("add", "add1");
    mx::NodePtr add2 = nodeGraph->addNode("add", "add2");
    mx::NodePtr add3 = nodeGraph->addNode("add", "add3");
    mx::NodePtr noise3d = nodeGraph->addNode("noise3d", "noise3d");
    mx::NodePtr mix = nodeGraph->addNode("mix", "mix");
    mx::OutputPtr out1 = nodeGraph->addOutput("out1");
    mx::OutputPtr out2 = nodeGraph->addOutput("out2");
    add1->setConnectedNode("in1", constant1);
    add1->setConnectedNode("in2", constant2);
    add2->setConnectedNode("in1", constant2);
    add2->setConnectedNode("in2", image2);
    add3->setConnectedNode("in1", add1);
    add3->setConnectedNode("in2", add2);
    multiply1->setConnectedNode("in1", image1);
    multiply1->setConnectedNode("in2", add1);
    multiply2->setConnectedNode("in1", noise3d);
    multiply2->setConnectedNode("in2", constant3);
    mix->setConnectedNode("fg", multiply1);
    mix->setConnectedNode("bg", add3);
    mix->setConnectedNode("mask", noise3d);
    out1->setConnectedNode(mix);
    out2->setConnectedNode(multiply2);

    // Validate the document.
    REQUIRE(doc->validate());

    // Vector of the outputs to generate new graphs from.
    std::vector<mx::OutputPtr> outputs = {out1, out2};

    // Blessed solution to a topological sort for the outputs.
    const std::vector<std::vector<std::string>> blessedElemOrder =
    {
        { "out1", "mix", "multiply1", "add3", "noise3d", "image1", "add1", "add2", "constant1", "constant2", "image2" },
        { "out2", "multiply2", "noise3d", "constant3" }
    };

    for (size_t i = 0; i < outputs.size(); ++i)
    {
        const mx::OutputPtr output = outputs[i];

        // Create a new graph with this output.
        mx::NodeGraphPtr nodeGraph2 = doc->addNodeGraph();
        nodeGraph2->addOutput(output->getName());

        // Keep track of processed nodes to avoid duplication
        // of nodes with multiple downstream connections.
        std::set<mx::NodePtr> processedNodes;

        for (mx::Edge edge : output->traverseGraph())
        {
            mx::NodePtr upstreamNode = edge.getUpstreamElement()->asA<mx::Node>();
            if (processedNodes.count(upstreamNode))
            {
                // Node is already processed
                continue;
            }

            // Create this node in the new graph.
            mx::NodePtr newNode = nodeGraph2->addNode(upstreamNode->getCategory(), upstreamNode->getName());
            newNode->copyContentFrom(upstreamNode);

            // Connect the node to downstream element in the new graph.
            mx::ElementPtr downstreamElement = edge.getDownstreamElement();
            mx::ElementPtr connectingElement = edge.getConnectingElement();
            if (downstreamElement->isA<mx::Output>())
            {
                mx::OutputPtr downstream = nodeGraph2->getOutput(downstreamElement->getName());
                downstream->setConnectedNode(newNode);
            }
            else if (connectingElement)
            {
                mx::NodePtr downstream = nodeGraph2->getNode(downstreamElement->getName());
                downstream->setConnectedNode(connectingElement->getName(), newNode);
            }

            // Mark node as processed.
            processedNodes.insert(upstreamNode);
        }

        // Create a topological order and test it agains the blessed solution.
        std::vector<mx::ElementPtr> elemOrder = nodeGraph2->topologicalSort();
        REQUIRE(elemOrder.size() == blessedElemOrder[i].size());
        for (size_t j = 0; j < elemOrder.size(); ++j)
        {
            REQUIRE(elemOrder[j]->getName() == blessedElemOrder[i][j]);
        }
    }
}

TEST_CASE("Prune nodes", "[nodegraph]")
{
    // Create a document.
    mx::DocumentPtr doc = mx::createDocument();

    // Create a node graph with the following structure:
    //
    //   [constant1] [constant2]      [image2]
    //           \   /          \    /
    // [image1] [add1]          [add2]
    //        \  /   \______      |   
    //    [multiply]        \__ [add3]         [noise3d]
    //             \____________  |  ____________/
    //                          [mix]
    //                            |
    //                         [output]
    //
    mx::NodeGraphPtr nodeGraph = doc->addNodeGraph();
    mx::NodePtr image1 = nodeGraph->addNode("image", "image1");
    mx::NodePtr image2 = nodeGraph->addNode("image", "image2");
    mx::NodePtr multiply = nodeGraph->addNode("multiply", "multiply");
    mx::NodePtr constant1 = nodeGraph->addNode("constant", "constant1");
    mx::NodePtr constant2 = nodeGraph->addNode("constant", "constant2");
    mx::NodePtr add1 = nodeGraph->addNode("add", "add1");
    mx::NodePtr add2 = nodeGraph->addNode("add", "add2");
    mx::NodePtr add3 = nodeGraph->addNode("add", "add3");
    mx::NodePtr noise3d = nodeGraph->addNode("noise3d", "noise3d");
    mx::NodePtr mix = nodeGraph->addNode("mix", "mix");
    mx::OutputPtr output = nodeGraph->addOutput("output");
    add1->setConnectedNode("in1", constant1);
    add1->setConnectedNode("in2", constant2);
    add2->setConnectedNode("in1", constant2);
    add2->setConnectedNode("in2", image2);
    add3->setConnectedNode("in1", add1);
    add3->setConnectedNode("in2", add2);
    multiply->setConnectedNode("in1", image1);
    multiply->setConnectedNode("in2", add1);
    mix->setConnectedNode("fg", multiply);
    mix->setConnectedNode("bg", add3);
    mix->setConnectedNode("mask", noise3d);
    output->setConnectedNode(mix);

    // Validate the document.
    REQUIRE(doc->validate());

    // Set the node names we want to prune from the graph
    // and which corresponding input to use for the bypass.
    std::unordered_map<std::string, std::string> nodesToPrune =
    {
        { "add1","in1" },
        { "add2","in1" },
        { "add3","in1" }
    };

    // Keep track of processed nodes to avoid duplication
    // of nodes with multiple downstream connections.
    std::set<mx::NodePtr> processedNodes;

    // Create the new graph with this output and traverse the
    // original graph upstream to find which nodes to copy.
    mx::NodeGraphPtr nodeGraph2 = doc->addNodeGraph();
    nodeGraph2->addOutput(output->getName());

    for (mx::Edge edge : output->traverseGraph())
    {
        mx::NodePtr upstreamNode = edge.getUpstreamElement()->asA<mx::Node>();
        if (processedNodes.count(upstreamNode))
        {
            // Node is already processed.
            continue;
        }

        // Find the downstream element in the new graph.
        mx::ElementPtr downstreamElement = edge.getDownstreamElement();
        mx::ElementPtr downstreamElement2 = nodeGraph2->getChild(downstreamElement->getName());
        if (!downstreamElement2)
        {
            // Downstream element has been pruned
            // so ignore this edge.
            continue;
        }

        // Check if this node should be pruned.
        // If so we travers upstream using the bypass inputs
        // until a non-prune node is found.
        mx::ValuePtr value;
        while (upstreamNode)
        {
            if (!nodesToPrune.count(upstreamNode->getName()))
            {
                break;
            }
            const std::string& inputName = nodesToPrune[upstreamNode->getName()];
            upstreamNode = upstreamNode->getConnectedNode(inputName);
        }

        if(upstreamNode)
        {
            // Get (or create) the node in the new graph.
            mx::NodePtr upstreamNode2 = nodeGraph2->getNode(upstreamNode->getName());
            if (!upstreamNode2)
            {
                upstreamNode2 = nodeGraph2->addNode(upstreamNode->getCategory(), upstreamNode->getName());
                upstreamNode2->copyContentFrom(upstreamNode);
            }

            mx::ElementPtr connectingElement = edge.getConnectingElement();

            // Connect it to downstream.
            // The downstream element could be a node or an output.
            mx::NodePtr downstreamNode2 = downstreamElement2->asA<mx::Node>();
            mx::OutputPtr downstreamOutput2 = downstreamElement2->asA<mx::Output>();
            if (downstreamOutput2)
            {
                downstreamOutput2->setConnectedNode(upstreamNode2);
            }
            else if (downstreamNode2 && connectingElement)
            {
                downstreamNode2->setConnectedNode(connectingElement->getName(), upstreamNode2);
            }
        }

        // Mark node as processed.
        processedNodes.insert(upstreamNode);
    }

    // Blessed solution to a topological sort of the
    // prunned graph.
    const std::vector<std::string> blessedElemOrder = 
    {
        "output",
        "mix",
        "multiply",
        "noise3d",
        "image1",
        "constant1"
    };

    // Create a topological order and test it agains the blessed solution.
    std::vector<mx::ElementPtr> elemOrder = nodeGraph2->topologicalSort();
    REQUIRE(elemOrder.size() == blessedElemOrder.size());
    for (size_t i = 0; i < elemOrder.size(); ++i)
    {
        REQUIRE(elemOrder[i]->getName() == blessedElemOrder[i]);
    }
}