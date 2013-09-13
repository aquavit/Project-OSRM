/*
    open source routing machine
    Copyright (C) Dennis Luxen, others 2010

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

#ifndef DISTANCEMATRIXPLUGIN_H_
#define DISTANCEMATRIXPLUGIN_H_



#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "BasePlugin.h"

#include "../Algorithms/ObjectToBase64.h"
#include "../DataStructures/HashTable.h"
#include "../DataStructures/QueryEdge.h"
#include "../DataStructures/StaticGraph.h"
#include "../DataStructures/SearchEngine.h"
#include "../Descriptors/BaseDescriptor.h"
#include "../Descriptors/GPXDescriptor.h"
#include "../Descriptors/JSONDescriptor.h"
#include "../Server/DataStructures/QueryObjectsStorage.h"
#include "../Util/SimpleLogger.h"
#include "../Util/StringUtil.h"


class DistanceMatrixPlugin : public BasePlugin {
private:
    NodeInformationHelpDesk * nodeHelpDesk;
    std::vector<std::string> & names;
    StaticGraph<QueryEdge::EdgeData> * graph;
    HashTable<std::string, unsigned> descriptorTable;
    SearchEngine* searchEngine;
    std::string descriptor_string;
public:

    DistanceMatrixPlugin(QueryObjectsStorage * objects) : names(objects->names), descriptor_string("distmatrix") {
        nodeHelpDesk = objects->nodeHelpDesk;
        graph = objects->graph;

        searchEngine = new SearchEngine(graph, nodeHelpDesk, names);

        descriptorTable.insert(std::make_pair(""    , 0));
        descriptorTable.insert(std::make_pair("json", 0));
        descriptorTable.insert(std::make_pair("gpx", 1));
    }

    virtual ~DistanceMatrixPlugin() {
        delete searchEngine;
    }

    const std::string& GetDescriptor() const { return descriptor_string; }
    std::string GetVersionString() const { return std::string("0.3 (DL)"); }
    void HandleRequest(const RouteParameters & routeParameters, http::Reply& reply) {
        //check number of parameters
        if( 2 > routeParameters.coordinates.size() || 500 < routeParameters.coordinates.size()) {
            reply = http::Reply::stockReply(http::Reply::badRequest);
            return;
        }

        RawRouteData rawRoute;
        rawRoute.checkSum = nodeHelpDesk->GetCheckSum();
        bool checksumOK = (routeParameters.checkSum == rawRoute.checkSum);
        std::vector<std::string> textCoord;
        for(unsigned i = 0; i < routeParameters.coordinates.size(); ++i) {
            if(false == checkCoord(routeParameters.coordinates[i])) {
                reply = http::Reply::stockReply(http::Reply::badRequest);
                return;
            }
            rawRoute.rawViaNodeCoordinates.push_back(routeParameters.coordinates[i]);
        }
        std::vector<PhantomNode> phantomNodeVector(rawRoute.rawViaNodeCoordinates.size());
        for(unsigned i = 0; i < rawRoute.rawViaNodeCoordinates.size(); ++i) {
            if(checksumOK && i < routeParameters.hints.size() && "" != routeParameters.hints[i]) {
//                INFO("Decoding hint: " << routeParameters.hints[i] << " for location index " << i);
                DecodeObjectFromBase64(routeParameters.hints[i], phantomNodeVector[i]);
                if(phantomNodeVector[i].isValid(nodeHelpDesk->getNumberOfNodes())) {
//                    INFO("Decoded hint " << i << " successfully");
                    continue;
                }
            }
//            INFO("Brute force lookup of coordinate " << i);
            searchEngine->FindPhantomNodeForCoordinate( rawRoute.rawViaNodeCoordinates[i], phantomNodeVector[i], routeParameters.zoomLevel);
        }

        reply.status = http::Reply::ok;

        //TODO: Move to member as smart pointer
        if("" != routeParameters.jsonpParameter) {
            reply.content += routeParameters.jsonpParameter;
            reply.content += "(";
        }


        unsigned descriptorType = descriptorTable[routeParameters.outputFormat];
        std::string sep="";           
        std::string arr="["; 
        for(unsigned i = 0; i < phantomNodeVector.size(); ++i) {
            for(unsigned j = 0; j < phantomNodeVector.size(); ++j) {
               if (i == j) continue;
                RawRouteData rawRouteLocal;
                PhantomNodes phantomNodesPair;
                phantomNodesPair.startPhantom = phantomNodeVector[i];
                phantomNodesPair.targetPhantom = phantomNodeVector[j];
                rawRouteLocal.segmentEndCoordinates.clear();
                rawRouteLocal.segmentEndCoordinates.push_back(phantomNodesPair);

                searchEngine->shortestPath(rawRouteLocal.segmentEndCoordinates, rawRouteLocal);

                if(INT_MAX == rawRouteLocal.lengthOfShortestPath ) {
                    SimpleLogger().Write(logDEBUG) << "Error occurred, single path not found";
                }
                
                PhantomNodes phantomNodes;
                phantomNodes.startPhantom = rawRouteLocal.segmentEndCoordinates[0].startPhantom;
                phantomNodes.targetPhantom = rawRouteLocal.segmentEndCoordinates[rawRouteLocal.segmentEndCoordinates.size()-1].targetPhantom;

                BaseDescriptor *desc;
                _DescriptorConfig descriptorConfig;
                descriptorConfig.z = routeParameters.zoomLevel;
                descriptorConfig.instructions = routeParameters.printInstructions;
                descriptorConfig.geometry = routeParameters.geometry;
                descriptorConfig.encodeGeometry = routeParameters.compression;
//                descriptorConfig.encodeGeometry = false;

                switch(descriptorType){
                case 0:
//                    desc = new JSONDescriptor<SearchEngine<QueryEdge::EdgeData, StaticGraph<QueryEdge::EdgeData> > >();
                    desc = new JSONDescriptor();
                    break;
                case 1:
//                    desc = new GPXDescriptor<SearchEngine<QueryEdge::EdgeData, StaticGraph<QueryEdge::EdgeData> > >();
                    desc = new GPXDescriptor();
                    break;
                default:
//                    desc = new JSONDescriptor<SearchEngine<QueryEdge::EdgeData, StaticGraph<QueryEdge::EdgeData> > >();
                    desc = new JSONDescriptor();

                    break;
                }
                desc->SetConfig(descriptorConfig);
                http::Reply partReply;
                desc->Run(partReply, rawRouteLocal, phantomNodes, *searchEngine);
                arr += sep;
                arr += partReply.content;
                sep = ",";
                delete desc;
            }
        }
        reply.content += arr + "]";
        if("" != routeParameters.jsonpParameter) {
            reply.content += ")\n";
        }

        reply.headers.resize(3);
        reply.headers[0].name = "Content-Length";
        std::string tmp;
        intToString(reply.content.size(), tmp);
        reply.headers[0].value = tmp;
        switch(descriptorType){
        case 0:
            if("" != routeParameters.jsonpParameter){
                reply.headers[1].name = "Content-Type";
                reply.headers[1].value = "text/javascript";
                reply.headers[2].name = "Content-Disposition";
                reply.headers[2].value = "attachment; filename=\"route.js\"";
            } else {
                reply.headers[1].name = "Content-Type";
                reply.headers[1].value = "application/x-javascript";
                reply.headers[2].name = "Content-Disposition";
                reply.headers[2].value = "attachment; filename=\"route.json\"";
            }

            break;
        case 1:
            reply.headers[1].name = "Content-Type";
            reply.headers[1].value = "application/gpx+xml; charset=UTF-8";
            reply.headers[2].name = "Content-Disposition";
            reply.headers[2].value = "attachment; filename=\"route.gpx\"";

            break;
        default:
            if("" != routeParameters.jsonpParameter){
                reply.headers[1].name = "Content-Type";
                reply.headers[1].value = "text/javascript";
                reply.headers[2].name = "Content-Disposition";
                reply.headers[2].value = "attachment; filename=\"route.js\"";
            } else {
                reply.headers[1].name = "Content-Type";
                reply.headers[1].value = "application/x-javascript";
                reply.headers[2].name = "Content-Disposition";
                reply.headers[2].value = "attachment; filename=\"route.json\"";
            }

            break;
        }

        return;
    }
};


#endif /* DISTANCEMATRIXPLUGIN_H_ */
