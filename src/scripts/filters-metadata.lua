-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- Receive script arguments
local config = ... or {}
config["filters"] = config["filters"] or {}
config["targets"] = config["targets"] or {}

f_metadata = ImplMetadata("filters")
f_metadata:activate(Features.ALL, function (m, e)
  if e then
    Log.warning("failed to activate filters metadata: " .. tostring(e))
    return
  end

  Log.info("activated filters metadata")

  -- Set sink filters in the metadata
  local sinks = {}
  for _, f in ipairs(config["filters"]) do
    if f.direction == "input" then
      table.insert (sinks, Json.Object (f))
    end
  end
  sinks_json = Json.Array (sinks)
  m:set (0, "filters.configured.inputs", "Spa:String:JSON",
          sinks_json:to_string())

  -- Set source filters in the metadata
  local sources = {}
  for _, f in ipairs(config["filters"]) do
    if f.direction == "output" then
      table.insert (sources, Json.Object (f))
    end
  end
  sources_json = Json.Array (sources)
  m:set (0, "filters.configured.outputs", "Spa:String:JSON",
          sources_json:to_string())

  -- Set targets metadata
  local targets = {}
  for name, props in pairs(config["targets"]) do
    targets[name] = Json.Object (props)
  end
  targets_json = Json.Object (targets)
  m:set (0, "filters.configured.targets", "Spa:String:JSON",
          targets_json:to_string())
end)
