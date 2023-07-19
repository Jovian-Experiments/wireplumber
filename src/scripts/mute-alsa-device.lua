-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

alsa_devices_om = ObjectManager {
  Interest {
    type = "device",
    Constraint { "device.api", "=", "alsa" },
  }
}

nodes_om = ObjectManager {
  Interest {
    type = "node",
    Constraint { "media.class", "=", "Audio/Sink", type = "pw-global"}
  }
}


local function parseParam(param_to_parse, id)
  local param = param_to_parse:parse()
  if param.pod_type == "Object" and param.object_id == id then
    return param.properties
  else
    return nil
  end
end

local function setRouteMute (device, route, mute)
  local dev_name = device.properties["device.name"]
  local param = Pod.Object {
    "Spa:Pod:Object:Param:Route", "Route",
    index = route.index,
    device = route.device,
    props = Pod.Object {
      "Spa:Pod:Object:Param:Props", "Route",
      mute = mute,
    },
    save = false,
  }
  Log.info (device, "Setting mute to " .. tostring(mute) .. " on route " ..
      route.name .. " for device " .. dev_name)
  device:set_param("Route", param)
end


local function muteAlsaDevicesTemporary (timeout_seconds)
  for alsa_dev in alsa_devices_om:iterate() do
    -- Mute device routes
    for p in alsa_dev:iterate_params("Route") do
      local route = parseParam(p, "Route")
      if route and
          route.direction == "Output" and
          not route.props.properties.mute then
        setRouteMute (alsa_dev, route, true)
      end
    end

    -- Unmute device routes after 3 seconds
    Core.timeout_add (timeout_seconds * 1000, function()
      for p in alsa_dev:iterate_params("Route") do
        local route = parseParam(p, "Route")
        if route and
	    route.direction == "Output" and
	    route.props.properties.mute then
          setRouteMute (alsa_dev, route, false)
        end
      end
    end)
  end

end

nodes_om:connect("object-removed", function (_, node)
  local device_api = node.properties['device.api']
  if device_api == "bluez5" then
    muteAlsaDevicesTemporary (3)
  end
end)

alsa_devices_om:activate()
nodes_om:activate()
