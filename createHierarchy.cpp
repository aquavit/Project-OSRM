/*
    open source routing machine
    Copyright (C) Dennis Luxen, 2010

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU AFFERO General Public License as published by
the Free Software Foundation; either version 3 of the License, or
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
or see http://www.gnu.org/licenses/agpl.txt.
 */

#include "Algorithms/IteratorBasedCRC32.h"
#include "Contractor/Contractor.h"
#include "Contractor/EdgeBasedGraphFactory.h"
#include "DataStructures/BinaryHeap.h"
#include "DataStructures/DeallocatingVector.h"
#include "DataStructures/QueryEdge.h"
#include "DataStructures/StaticGraph.h"
#include "DataStructures/StaticRTree.h"
#include "Util/IniFile.h"
#include "Util/GraphLoader.h"
#include "Util/InputFileUtil.h"
#include "Util/LuaUtil.h"
#include "Util/OpenMPWrapper.h"
#include "Util/OSRMException.h"
#include "Util/SimpleLogger.h"
#include "Util/StringUtil.h"
#include "typedefs.h"

#include <boost/foreach.hpp>

#include <luabind/luabind.hpp>

#include <fstream>
#include <istream>
#include <iostream>
#include <cstring>
#include <string>
#include <vector>

typedef QueryEdge::EdgeData EdgeData;
typedef DynamicGraph<EdgeData>::InputEdge InputEdge;
typedef StaticGraph<EdgeData>::InputEdge StaticEdge;
typedef IniFile ContractorConfiguration;

std::vector<NodeInfo> internalToExternalNodeMapping;
std::vector<TurnRestriction> inputRestrictions;
std::vector<NodeID> bollardNodes;
std::vector<NodeID> trafficLightNodes;
std::vector<ImportEdge> edgeList;

int main (int argc, char *argv[]) {
    try {
        LogPolicy::GetInstance().Unmute();
        if(argc < 3) {
            SimpleLogger().Write(logWARNING) <<
                "usage: \n" <<
                argv[0] << " <osrm-data> <osrm-restrictions> [<profile>]";
            return -1;
        }

        double startupTime = get_timestamp();
        unsigned number_of_threads = omp_get_num_procs();
        if(testDataFile("contractor.ini")) {
            ContractorConfiguration contractorConfig("contractor.ini");
            unsigned rawNumber = stringToInt(contractorConfig.GetParameter("Threads"));
            if(rawNumber != 0 && rawNumber <= number_of_threads)
                number_of_threads = rawNumber;
        }
        omp_set_num_threads(number_of_threads);
        LogPolicy::GetInstance().Unmute();
        SimpleLogger().Write() << "Using restrictions from file: " << argv[2];
        std::ifstream restrictionsInstream(argv[2], std::ios::binary);
        if(!restrictionsInstream.good()) {
            std::cerr <<
                "Could not access <osrm-restrictions> files" << std::endl;

        }
        TurnRestriction restriction;
        UUID uuid_loaded, uuid_orig;
        unsigned usableRestrictionsCounter(0);
        restrictionsInstream.read((char*)&uuid_loaded, sizeof(UUID));
        if( !uuid_loaded.TestPrepare(uuid_orig) ) {
            SimpleLogger().Write(logWARNING) <<
                ".restrictions was prepared with different build.\n"
                "Reprocess to get rid of this warning.";
        }

        restrictionsInstream.read(
            (char*)&usableRestrictionsCounter,
            sizeof(unsigned)
        );
        inputRestrictions.resize(usableRestrictionsCounter);
        restrictionsInstream.read(
            (char *)&(inputRestrictions[0]),
            usableRestrictionsCounter*sizeof(TurnRestriction)
        );
        restrictionsInstream.close();

        std::ifstream in;
        in.open (argv[1], std::ifstream::in | std::ifstream::binary);
        if (!in.is_open()) {
            throw OSRMException("Cannot open osrm input file");
        }

        std::string nodeOut(argv[1]);		nodeOut += ".nodes";
        std::string edgeOut(argv[1]);		edgeOut += ".edges";
        std::string graphOut(argv[1]);		graphOut += ".hsgr";
        std::string rtree_nodes_path(argv[1]);  rtree_nodes_path += ".ramIndex";
        std::string rtree_leafs_path(argv[1]);  rtree_leafs_path += ".fileIndex";

        /*** Setup Scripting Environment ***/
        if(!testDataFile( (argc > 3 ? argv[3] : "profile.lua") )) {
            throw OSRMException("Cannot open profile.lua ");
        }

        // Create a new lua state
        lua_State *myLuaState = luaL_newstate();

        // Connect LuaBind to this lua state
        luabind::open(myLuaState);

        //open utility libraries string library;
        luaL_openlibs(myLuaState);

        //adjust lua load path
        luaAddScriptFolderToLoadPath( myLuaState, (argc > 3 ? argv[3] : "profile.lua") );

        // Now call our function in a lua script
        SimpleLogger().Write() <<
            "Parsing speedprofile from " <<
            (argc > 3 ? argv[3] : "profile.lua");

        if(0 != luaL_dofile(myLuaState, (argc > 3 ? argv[3] : "profile.lua") )) {
            std::cerr <<
                lua_tostring(myLuaState,-1)   <<
                " occured in scripting block" <<
                std::endl;
        }

        EdgeBasedGraphFactory::SpeedProfileProperties speedProfile;

        if(0 != luaL_dostring( myLuaState, "return traffic_signal_penalty\n")) {
            std::cerr <<
                lua_tostring(myLuaState,-1) <<
                " occured in scripting block" <<
                std::endl;
                return -1;
        }
        speedProfile.trafficSignalPenalty = 10*lua_tointeger(myLuaState, -1);

        if(0 != luaL_dostring( myLuaState, "return u_turn_penalty\n")) {
            std::cerr <<
                lua_tostring(myLuaState,-1)   <<
                " occured in scripting block" <<
                std::endl;
            return -1;
        }
        speedProfile.uTurnPenalty = 10*lua_tointeger(myLuaState, -1);

        speedProfile.has_turn_penalty_function = lua_function_exists( myLuaState, "turn_function" );

        std::vector<ImportEdge> edgeList;
        NodeID nodeBasedNodeNumber = readBinaryOSRMGraphFromStream(in, edgeList, bollardNodes, trafficLightNodes, &internalToExternalNodeMapping, inputRestrictions);
        in.close();
        SimpleLogger().Write() <<
            inputRestrictions.size() <<
            " restrictions, " <<
            bollardNodes.size() <<
            " bollard nodes, " <<
            trafficLightNodes.size() <<
            " traffic lights";

        if(0 == edgeList.size()) {
            std::cerr <<
                "The input data is broken. "
                "It is impossible to do any turns in this graph" <<
                std::endl;
            return -1;
        }

        /***
         * Building an edge-expanded graph from node-based input an turn restrictions
         */

        SimpleLogger().Write() << "Generating edge-expanded graph representation";
        EdgeBasedGraphFactory * edgeBasedGraphFactory = new EdgeBasedGraphFactory (nodeBasedNodeNumber, edgeList, bollardNodes, trafficLightNodes, inputRestrictions, internalToExternalNodeMapping, speedProfile);
        std::vector<ImportEdge>().swap(edgeList);
        edgeBasedGraphFactory->Run(edgeOut.c_str(), myLuaState);
        std::vector<TurnRestriction>().swap(inputRestrictions);
        std::vector<NodeID>().swap(bollardNodes);
        std::vector<NodeID>().swap(trafficLightNodes);
        NodeID edgeBasedNodeNumber = edgeBasedGraphFactory->GetNumberOfNodes();
        DeallocatingVector<EdgeBasedEdge> edgeBasedEdgeList;
        edgeBasedGraphFactory->GetEdgeBasedEdges(edgeBasedEdgeList);
        std::vector<EdgeBasedGraphFactory::EdgeBasedNode> nodeBasedEdgeList;
        edgeBasedGraphFactory->GetEdgeBasedNodes(nodeBasedEdgeList);
        delete edgeBasedGraphFactory;

        /***
         * Writing info on original (node-based) nodes
         */

        SimpleLogger().Write() << "writing node map ...";
        std::ofstream mapOutFile(nodeOut.c_str(), std::ios::binary);
        mapOutFile.write((char *)&(internalToExternalNodeMapping[0]), internalToExternalNodeMapping.size()*sizeof(NodeInfo));
        mapOutFile.close();
        std::vector<NodeInfo>().swap(internalToExternalNodeMapping);

        double expansionHasFinishedTime = get_timestamp() - startupTime;

        /***
         * Building grid-like nearest-neighbor data structure
         */

        SimpleLogger().Write() << "building r-tree ...";
        StaticRTree<EdgeBasedGraphFactory::EdgeBasedNode> * rtree =
                new StaticRTree<EdgeBasedGraphFactory::EdgeBasedNode>(
                        nodeBasedEdgeList,
                        rtree_nodes_path.c_str(),
                        rtree_leafs_path.c_str()
                );
        delete rtree;
        IteratorbasedCRC32<std::vector<EdgeBasedGraphFactory::EdgeBasedNode> > crc32;
        unsigned crc32OfNodeBasedEdgeList = crc32(nodeBasedEdgeList.begin(), nodeBasedEdgeList.end() );
        nodeBasedEdgeList.clear();
        SimpleLogger().Write() << "CRC32: " << crc32OfNodeBasedEdgeList;

        /***
         * Contracting the edge-expanded graph
         */

        SimpleLogger().Write() << "initializing contractor";
        Contractor* contractor = new Contractor( edgeBasedNodeNumber, edgeBasedEdgeList );
        double contractionStartedTimestamp(get_timestamp());
        contractor->Run();
        const double contraction_duration = (get_timestamp() - contractionStartedTimestamp);
        SimpleLogger().Write() <<
            "Contraction took " <<
            contraction_duration <<
            " sec";

        DeallocatingVector< QueryEdge > contractedEdgeList;
        contractor->GetEdges( contractedEdgeList );
        delete contractor;

        /***
         * Sorting contracted edges in a way that the static query graph can read some in in-place.
         */

        SimpleLogger().Write() << "Building Node Array";
        std::sort(contractedEdgeList.begin(), contractedEdgeList.end());
        unsigned numberOfNodes = 0;
        unsigned numberOfEdges = contractedEdgeList.size();
        SimpleLogger().Write() <<
            "Serializing compacted graph of " <<
            numberOfEdges <<
            " edges";

        std::ofstream hsgr_output_stream(graphOut.c_str(), std::ios::binary);
        hsgr_output_stream.write((char*)&uuid_orig, sizeof(UUID) );
        BOOST_FOREACH(const QueryEdge & edge, contractedEdgeList) {
            if(edge.source > numberOfNodes) {
                numberOfNodes = edge.source;
            }
            if(edge.target > numberOfNodes) {
                numberOfNodes = edge.target;
            }
        }
        numberOfNodes+=1;

        std::vector< StaticGraph<EdgeData>::_StrNode > _nodes;
        _nodes.resize( numberOfNodes + 1 );

        StaticGraph<EdgeData>::EdgeIterator edge = 0;
        StaticGraph<EdgeData>::EdgeIterator position = 0;
        for ( StaticGraph<EdgeData>::NodeIterator node = 0; node <= numberOfNodes; ++node ) {
            StaticGraph<EdgeData>::EdgeIterator lastEdge = edge;
            while ( edge < numberOfEdges && contractedEdgeList[edge].source == node )
                ++edge;
            _nodes[node].firstEdge = position; //=edge
            position += edge - lastEdge; //remove
        }
        ++numberOfNodes;
        //Serialize numberOfNodes, nodes
        hsgr_output_stream.write((char*) &crc32OfNodeBasedEdgeList, sizeof(unsigned));
        hsgr_output_stream.write((char*) &numberOfNodes, sizeof(unsigned));
        hsgr_output_stream.write((char*) &_nodes[0], sizeof(StaticGraph<EdgeData>::_StrNode)*(numberOfNodes));
        //Serialize number of Edges
        hsgr_output_stream.write((char*) &position, sizeof(unsigned));
        --numberOfNodes;
        edge = 0;
        int usedEdgeCounter = 0;
        StaticGraph<EdgeData>::_StrEdge currentEdge;
        for ( StaticGraph<EdgeData>::NodeIterator node = 0; node < numberOfNodes; ++node ) {
            for ( StaticGraph<EdgeData>::EdgeIterator i = _nodes[node].firstEdge, e = _nodes[node+1].firstEdge; i != e; ++i ) {
                assert(node != contractedEdgeList[edge].target);
                currentEdge.target = contractedEdgeList[edge].target;
                currentEdge.data = contractedEdgeList[edge].data;
                if(currentEdge.data.distance <= 0) {
                    SimpleLogger().Write(logWARNING) <<
                        "Edge: "     << i <<
                        ",source: "  << contractedEdgeList[edge].source <<
                        ", target: " << contractedEdgeList[edge].target <<
                        ", dist: "   << currentEdge.data.distance;

                    SimpleLogger().Write(logWARNING) <<
                        "Failed at edges of node " << node <<
                        " of " << numberOfNodes;
                        return -1;
                }
                //Serialize edges
                hsgr_output_stream.write((char*) &currentEdge, sizeof(StaticGraph<EdgeData>::_StrEdge));
                ++edge;
                ++usedEdgeCounter;
            }
        }
        SimpleLogger().Write() << "Preprocessing : " <<
            (get_timestamp() - startupTime) << " seconds";
        SimpleLogger().Write() << "Expansion  : " <<
            (nodeBasedNodeNumber/expansionHasFinishedTime) << " nodes/sec and " <<
            (edgeBasedNodeNumber/expansionHasFinishedTime) << " edges/sec";

        SimpleLogger().Write() << "Contraction: " <<
            (edgeBasedNodeNumber/contraction_duration) << " nodes/sec and " <<
            usedEdgeCounter/contraction_duration << " edges/sec";

        hsgr_output_stream.close();
        //cleanedEdgeList.clear();
        _nodes.clear();
        SimpleLogger().Write() << "finished preprocessing";
    } catch ( const std::exception &e ) {
        SimpleLogger().Write(logWARNING) <<
            "Exception occured: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}
