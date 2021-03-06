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

#ifndef NearestPlugin_H_
#define NearestPlugin_H_

#include "BasePlugin.h"

#include "../DataStructures/NodeInformationHelpDesk.h"
#include "../Server/DataStructures/QueryObjectsStorage.h"
#include "../Util/StringUtil.h"

/*
 * This Plugin locates the nearest point on a street in the road network for a given coordinate.
 */
class NearestPlugin : public BasePlugin {
public:
    NearestPlugin(QueryObjectsStorage * objects )
     :
        names(objects->names),
        descriptor_string("nearest")
    {
        nodeHelpDesk = objects->nodeHelpDesk;

        descriptorTable.insert(std::make_pair(""    , 0)); //default descriptor
        descriptorTable.insert(std::make_pair("json", 1));
    }
    const std::string & GetDescriptor() const { return descriptor_string; }
    void HandleRequest(const RouteParameters & routeParameters, http::Reply& reply) {
        //check number of parameters
        if(!routeParameters.coordinates.size()) {
            reply = http::Reply::stockReply(http::Reply::badRequest);
            return;
        }
        if(false == checkCoord(routeParameters.coordinates[0])) {
            reply = http::Reply::stockReply(http::Reply::badRequest);
            return;
        }

        //query to helpdesk
        PhantomNode result;
        nodeHelpDesk->FindPhantomNodeForCoordinate(routeParameters.coordinates[0], result, routeParameters.zoomLevel);

        std::string tmp;
        //json

        if("" != routeParameters.jsonpParameter) {
            reply.content += routeParameters.jsonpParameter;
            reply.content += "(";
        }

        reply.status = http::Reply::ok;
        reply.content += ("{");
        reply.content += ("\"version\":0.3,");
        reply.content += ("\"status\":");
        if(UINT_MAX != result.edgeBasedNode)
            reply.content += "0,";
        else
            reply.content += "207,";
        reply.content += ("\"mapped_coordinate\":");
        reply.content += "[";
        if(UINT_MAX != result.edgeBasedNode) {
            convertInternalLatLonToString(result.location.lat, tmp);
            reply.content += tmp;
            convertInternalLatLonToString(result.location.lon, tmp);
            reply.content += ",";
            reply.content += tmp;
        }
        reply.content += "],";
        reply.content += "\"name\":\"";
        if(UINT_MAX != result.edgeBasedNode)
            reply.content += names[result.nodeBasedEdgeNameID];
        reply.content += "\"";
        reply.content += ",\"transactionId\":\"OSRM Routing Engine JSON Nearest (v0.3)\"";
        reply.content += ("}");
        reply.headers.resize(3);
        if("" != routeParameters.jsonpParameter) {
            reply.content += ")";
            reply.headers[1].name = "Content-Type";
            reply.headers[1].value = "text/javascript";
            reply.headers[2].name = "Content-Disposition";
            reply.headers[2].value = "attachment; filename=\"location.js\"";
        } else {
            reply.headers[1].name = "Content-Type";
            reply.headers[1].value = "application/x-javascript";
            reply.headers[2].name = "Content-Disposition";
            reply.headers[2].value = "attachment; filename=\"location.json\"";
        }
        reply.headers[0].name = "Content-Length";
        intToString(reply.content.size(), tmp);
        reply.headers[0].value = tmp;
    }

private:
    NodeInformationHelpDesk * nodeHelpDesk;
    HashTable<std::string, unsigned> descriptorTable;
    std::vector<std::string> & names;
    std::string descriptor_string;
};

#endif /* NearestPlugin_H_ */
