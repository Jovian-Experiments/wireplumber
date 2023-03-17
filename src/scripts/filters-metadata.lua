-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- Receive script arguments
local config = ... or {}
config["sink-filters"] = config["sink-filters"] or {}
config["source-filters"] = config["source-filters"] or {}
config["groups-target"] = config["groups-target"] or {}

f_metadata = ImplMetadata("filters")
f_metadata:activate(Features.ALL, function (m, e)
  if e then
    Log.warning("failed to activate filters metadata: " .. tostring(e))
    return
  end

  Log.info("activated filters metadata")

  -- Set sink filters in the metadata
  local sinks = {}
  for _, f in ipairs(config["sink-filters"]) do
    table.insert (sinks, Json.Object (f))
  end
  sinks_json = Json.Array (sinks)
  m:set (0, "filters.configured.audio.sink", "Spa:String:JSON",
          sinks_json:to_string())

  -- Set source filters in the metadata
  local sources = {}
  for _, f in ipairs(config["source-filters"]) do
    table.insert (sources, Json.Object (f))
  end
  sources_json = Json.Array (sources)
  m:set (0, "filters.configured.audio.source", "Spa:String:JSON",
          sources_json:to_string())

  -- Set groups target metadata
  local groups = {}
  for name, props in pairs(config["groups-target"]) do
    groups[name] = Json.Object (props)
  end
  groups_json = Json.Object (groups)
  m:set (0, "filters.configured.groups-target", "Spa:String:JSON",
          groups_json:to_string())
end)
