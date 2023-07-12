-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT

local cutils = require ("common-utils")

log = Log.open_topic ("s-monitors-libcam")

function createLibcamNode (parent, id, type, factory, properties)
  source = source or Plugin.find ("standard-event-source")

  local e = source:call ("create-event", "create-libcam-device-node",
    parent, properties)
  e:set_data ("factory", factory)
  e:set_data ("node-sub-id", id)

  EventDispatcher.push_event (e)
end

SimpleEventHook {
  name = "monitor/libcam/create-device",
  after = "monitor/libcam/name-device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-libcam-device" },
    },
  },
  execute = function(event)
    local properties = event:get_data ("device-properties")
    local factory = event:get_data ("factory")
    local parent = event:get_subject ()
    local id = event:get_data ("device-sub-id")

    -- apply properties from rules defined in JSON .conf file
    cutils.evaluateRulesApplyProperties (properties, "monitor.libcamera.rules")
    if properties ["device.disabled"] then
      log:warning ("lib cam device " .. properties ["device.name"] .. " disabled")
      return
    end
    local device = SpaDevice (factory, properties)

    if device then
      device:connect ("create-object", createLibcamNode)
      device:activate (Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
      parent:store_managed_object (id, device)
    else
      log:warning ("Failed to create '" .. factory .. "' device")
    end
  end
}:register ()
