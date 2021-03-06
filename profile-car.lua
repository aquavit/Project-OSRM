-- Begin of globals
require("lib/access")

barrier_whitelist = { ["cattle_grid"] = true, ["border_control"] = true, ["toll_booth"] = true, ["sally_port"] = true, ["gate"] = true}
access_tag_whitelist = { ["yes"] = true, ["motorcar"] = true, ["motor_vehicle"] = true, ["vehicle"] = true, ["permissive"] = true, ["designated"] = true  }
access_tag_blacklist = { ["no"] = true, ["private"] = true, ["agricultural"] = true, ["forestery"] = true }
access_tag_restricted = { ["destination"] = true, ["delivery"] = true }
access_tags = { "motorcar", "motor_vehicle", "vehicle" }
access_tags_hierachy = { "motorcar", "motor_vehicle", "vehicle", "access" }
service_tag_restricted = { ["parking_aisle"] = true }
ignore_in_grid = { ["ferry"] = true }
restriction_exception_tags = { "motorcar", "motor_vehicle", "vehicle" }

speed_profile = { 
  ["motorway"] = 60, 
  ["motorway_link"] = 45, 
  ["trunk"] = 50, 
  ["trunk_link"] = 30,
  ["primary"] = 40,
  ["primary_link"] = 30,
  ["secondary"] = 40,
  ["secondary_link"] = 30,
  ["tertiary"] = 30,
  ["tertiary_link"] = 20,
  ["unclassified"] = 15,
  ["residential"] = 15,
  ["living_street"] = 10,
  ["service"] = 15,
--  ["track"] = 5,
  ["ferry"] = 5,
  ["shuttle_train"] = 10,
  ["default"] = 30
}

take_minimum_of_speeds 	= false
obey_oneway 			= true
obey_bollards 			= true
use_restrictions 		= true
ignore_areas 			= true -- future feature
traffic_signal_penalty 	= 2
u_turn_penalty 			= 20
width_limit                     = 2.5

-- End of globals

--find first tag in access hierachy which is set
local function find_access_tag(source)
	for i,v in ipairs(access_tags_hierachy) do 
		if source.tags:Holds(v) then 
			local tag = source.tags:Find(v)
			if tag ~= '' then --and tag ~= "" then
				return tag
			end
		end
	end
	return nil
end

local function find_in_keyvals(keyvals, tag)
	if keyvals:Holds(tag) then
		return keyvals:Find(tag)
	else
		return nil
	end
end

local function parse_width(source)
	if source == nil then
		return 0
	end
	local n = tonumber(source)
	if n == nil then
		n = 0
	end
	
	return math.abs(n);
end

function get_exceptions(vector)
	for i,v in ipairs(restriction_exception_tags) do 
		vector:Add(v)
	end
end

local function parse_maxspeed(source)
	if source == nil then
		return 0
	end
	local n = tonumber(source:match("%d*"))
	if n == nil then
		n = 0
	end
	source = string.lower(source);
	if string.match(source, "mph") or string.match(source, "mp/h") then
		n = (n*1609)/1000;
	end
	if string.match(source, "kmh") or string.match(source, "km/h") or string.match(source, "kh") or string.match(source, "k/h") then
		n = (n*1000)/1000;
	end
	return math.abs(n)
end

function node_function (node)
  local barrier = node.tags:Find ("barrier")
  local access = Access.find_access_tag(node, access_tags_hierachy)
  local traffic_signal = node.tags:Find("highway")
  
  --flag node if it carries a traffic light
  
  if traffic_signal == "traffic_signals" then
	node.traffic_light = true;
  end
  
	-- parse access and barrier tags
	if access  and access ~= "" then
		if access_tag_blacklist[access] then
			node.bollard = true
		end
	elseif barrier and barrier ~= "" then
		if barrier_whitelist[barrier] then
			return
		else
			node.bollard = true
		end
	end
	return 1
end


function way_function (way, numberOfNodesInWay)

  -- A way must have two nodes or more
  if(numberOfNodesInWay ~= nil and numberOfNodesInWay < 2) then
    return 0;
  end
  
  -- First, get the properties of each way that we come across
    local highway = way.tags:Find("highway")
    local name = way.tags:Find("name")
    local ref = way.tags:Find("ref")
    local junction = way.tags:Find("junction")
    local route = way.tags:Find("route")
    local maxspeed = parse_maxspeed(way.tags:Find ( "maxspeed") )
    local maxspeed_forward = parse_maxspeed(way.tags:Find( "maxspeed:forward"))
    local maxspeed_backward = parse_maxspeed(way.tags:Find( "maxspeed:backward"))
    local width = parse_width(way.tags:Find("width"))
    local barrier = way.tags:Find("barrier")
    local oneway = way.tags:Find("oneway")
    local cycleway = way.tags:Find("cycleway")
    local duration  = way.tags:Find("duration")
    local service  = way.tags:Find("service")
    local area = way.tags:Find("area")
    local access = Access.find_access_tag(way, access_tags_hierachy)

  -- Check the way's width before proceeding
    if (width_limit > 0 and width > 0  and width > width_limit) then
         return 0
    end
  -- Second, parse the way according to these properties

	if ignore_areas and ("yes" == area) then
		return 0
	end
		
  	-- Check if we are allowed to access the way
    if access_tag_blacklist[access] then
		return 0
    end

  -- Set the name that will be used for instructions  
	if "" ~= ref then
	  way.name = ref
	elseif "" ~= name then
	  way.name = name
--	else
--      way.name = highway		-- if no name exists, use way type
	end
	
	if "roundabout" == junction then
	  way.roundabout = true;
	end

  -- Handling ferries and piers
    if (speed_profile[route] ~= nil and speed_profile[route] > 0) then
      if durationIsValid(duration) then
	    way.duration = math.max( parseDuration(duration), 1 );
      end
      way.direction = Way.bidirectional
      if speed_profile[route] ~= nil then
         highway = route;
      end
      if tonumber(way.duration) < 0 then
        way.speed = speed_profile[highway]
      end
    end
    
  -- Set the avg speed on the way if it is accessible by road class
  if (speed_profile[highway] ~= nil and way.speed == -1 ) then
    if maxspeed > speed_profile[highway] then
      way.speed = maxspeed
    else
      if 0 == maxspeed then
        maxspeed = math.huge
      end
      way.speed = math.min(speed_profile[highway], maxspeed)
    end
  end
    
  -- Set the avg speed on ways that are marked accessible
    if "" ~= highway and access_tag_whitelist[access] and way.speed == -1 then
      if 0 == maxspeed then
        maxspeed = math.huge
      end
      way.speed = math.min(speed_profile["default"], maxspeed)
    end
    
    if durationIsValid(duration) then
      way.duration = math.max( parseDuration(duration), 1 );
    end

  -- Set access restriction flag if access is allowed under certain restrictions only
    if access ~= "" and access_tag_restricted[access] then
	  way.is_access_restricted = true
    end

  -- Set access restriction flag if service is allowed under certain restrictions only
    if service ~= "" and service_tag_restricted[service] then
	  way.is_access_restricted = true
    end
    
  -- Set direction according to tags on way
    if obey_oneway then
      if oneway == "no" or oneway == "0" or oneway == "false" then
	    way.direction = Way.bidirectional
	  elseif oneway == "-1" then
	    way.direction = Way.opposite
      elseif oneway == "yes" or oneway == "1" or oneway == "true" or junction == "roundabout" or highway == "motorway_link" or highway == "motorway" then
		way.direction = Way.oneway
      else
        way.direction = Way.bidirectional
      end
    else
      way.direction = Way.bidirectional
    end
    
  -- Override speed settings if explicit forward/backward maxspeeds are given
    if maxspeed_forward ~= nil and maxspeed_forward > 0 then
	if Way.bidirectional == way.direction then
          way.backward_speed = way.speed
        end
        way.speed = maxspeed_forward
    end
    if maxspeed_backward ~= nil and maxspeed_backward > 0 then
      way.backward_speed = maxspeed_backward
    end
    
  -- Override general direction settings of there is a specific one for our mode of travel
  
    if ignore_in_grid[highway] ~= nil and ignore_in_grid[highway] then
		way.ignore_in_grid = true
  	end
  	way.type = 1
  return 1
end

-- These are wrappers to parse vectors of nodes and ways and thus to speed up any tracing JIT

function node_vector_function(vector)
 for v in vector.nodes do
  node_function(v)
 end
end
