-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

local self = {}
self.config = ... or {}
self.scanning = false
self.pending_rescan = false
self.default_nodes = Plugin.find("default-nodes-api")
self.metadata_om = ObjectManager {
  Interest {
    type = "metadata",
    Constraint { "metadata.name", "=", "default" },
  }
}
self.linkables_om = ObjectManager {
  Interest {
    type = "SiLinkable",
  }
}
self.links_om = ObjectManager {
  Interest {
    type = "SiLink",
    Constraint { "is.policy.item.link", "=", true },
  }
}
self.devices_om = ObjectManager { Interest { type = "device" } }

function rescan()
  for si in self.linkables_om:iterate() do
    handleLinkable (si)
  end
end

function scheduleRescan ()
  if self.scanning then
    self.pending_rescan = true
    return
  end

  self.scanning = true
  rescan ()
  self.scanning = false

  if self.pending_rescan then
    self.pending_rescan = false
    Core.sync(function ()
      scheduleRescan ()
    end)
  end
end

function getSiTarget (node_name)
  local si = self.linkables_om:lookup {
      Constraint { "node.name", "=", node_name } }
  if si == nil then
    return nil
  end

  for silink in self.links_om:iterate() do
    local out_id = tonumber (silink.properties["out.item.id"])
    local in_id = tonumber (silink.properties["in.item.id"])
    local si_target = nil
    if out_id == si.id then
       si_target = self.linkables_om:lookup {
          Constraint { "id", "=", in_id, type = "gobject" } }
    elseif in_id == si.id then
      si_target = self.linkables_om:lookup {
          Constraint { "id", "=", out_id, type = "gobject" } }
    end
    if si_target ~= nil and
        si_target.properties["item.node.type"] ~= "stream" then
      return si_target
    end
  end
  return nil
end

function handleDefaultNode (si, id, name, media_class)
  local metadata = self.metadata_om:lookup()
  if metadata == nil then
    return
  end

  local def_name = name
  if media_class == "Audio/Sink" then
    if name == "echo-cancel-sink" then
      local si_target = getSiTarget ("echo-cancel-playback")
      if si_target then
        local target_node = si_target:get_associated_proxy ("node")
        def_name = target_node.properties["node.name"]
        Log.info ("Echo cancel playback target " .. def_name)
      end
    elseif name == "filter-chain-sink" then
      local si_target = getSiTarget ("echo-cancel-playback")
      if si_target == nil then
        si_target = getSiTarget ("filter-chain-playback")
      end
      if si_target then
        local target_node = si_target:get_associated_proxy ("node")
        def_name = target_node.properties["node.name"]
        Log.info ("Filter chain playback target " .. def_name)
      end
    end
    Log.info ("Setting default.audio.sink to " .. def_name)
    metadata:set(0, "default.audio.sink", "Spa:String:JSON",
        "{ \"name\": \"" .. def_name .. "\" }")
  elseif media_class == "Audio/Source" then
    if name == "echo-cancel-source" then
      local si_target = getSiTarget ("echo-cancel-capture")
      if si_target then
        local target_node = si_target:get_associated_proxy ("node")
        def_name = target_node.properties["node.name"]
        Log.info ("Echo cancel capture target " .. def_name)
      end
    elseif name == "filter-chain-source" then
      local si_target = getSiTarget ("echo-cancel-capture")
      if si_target == nil then
        si_target = getSiTarget ("filter-chain-capture")
      end
      if si_target then
        local target_node = si_target:get_associated_proxy ("node")
        def_name = target_node.properties["node.name"]
        Log.info ("Filter chain capture target " .. def_name)
      end
    end
    Log.info ("Setting default.audio.source to " .. def_name)
    metadata:set(0, "default.audio.source", "Spa:String:JSON",
        "{ \"name\": \"" .. def_name .. "\" }")
  elseif media_class == "Video/Source" then
    Log.info ("Setting default.video.source to " .. def_name)
    metadata:set(0, "default.video.source", "Spa:String:JSON",
        "{ \"name\": \"" .. def_name .. "\" }")
  end
end

function handleLinkable (si)
  local node = si:get_associated_proxy ("node")
  if node == nil then
    return
  end

  local node_id = node["bound-id"]
  local media_classes = { "Audio/Sink", "Audio/Source", "Video/Source" }
  for _, media_class in ipairs(media_classes) do
    local def_id = self.default_nodes:call("get-default-node", media_class)
    if def_id ~= Id.INVALID and def_id == node_id then
      local node_name = node.properties["node.name"]
      handleDefaultNode (si, node_id, node_name, media_class)
      break
    end
  end
end

self.linkables_om:connect("objects-changed", function (om, si)
  scheduleRescan ()
end)

self.links_om:connect("objects-changed", function (om, si)
  scheduleRescan ()
end)

self.devices_om:connect("object-added", function (om, device)
  device:connect("params-changed", function (d, param_name)
    scheduleRescan ()
  end)
end)

self.metadata_om:activate()
self.linkables_om:activate()
self.links_om:activate()
self.devices_om:activate()
