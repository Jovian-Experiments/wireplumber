-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

local self = {}
self.config = ... or {}
self.config["sink-filters"] = self.config["sink-filters"] or {}
self.config["source-filters"] = self.config["source-filters"] or {}

-- The filters metadata
self.metadata_om = ObjectManager {
  Interest { type = "metadata",
    Constraint { "metadata.name", "=", "filters" },
  }
}

-- Only handle stream nodes that are not filters
self.nodes_om = ObjectManager {
  Interest {
    type = "node",
    Constraint { "media.class", "=", "Stream/Input/Audio" },
    Constraint { "node.link-group", "-", type = "pw"}
  }
}

function arrayContains (array, node_name)
  for _, v in ipairs(array) do
    if v == node_name then
      return true
    end
  end
  return false
end

function enableFilters (metadata_key, filters, enabled)
  local metadata = self.metadata_om:lookup()
  local changed = false

  if metadata == nil then
    return
  end

  local str = metadata:find(0, metadata_key)
  if str == nil then
    return
  end

  local json = Json.Raw (str)
  local json_parsed = json:parse()
  local filters_changed = {}
  for _, f in ipairs(json_parsed or {}) do
    if f.enabled ~= enabled then
      local node_name = f["node-name"]
      if node_name ~= nil and arrayContains(filters, node_name) then
        f.enabled = enabled
        changed = true
        Log.info ("Setting enabled on filter '" .. node_name .. "' to " ..
            tostring (enabled))
      end
    end
    table.insert (filters_changed, Json.Object (f))
  end

  if not changed then
    return
  end

  local json_changed = Json.Array (filters_changed)
  local json_changed_str = json_changed:to_string()

  metadata:set (0, metadata_key, "Spa:String:JSON", json_changed_str)
end

function checkFilters ()
  if self.nodes_om:get_n_objects () > 0 then
    enableFilters ("filters.configured.audio.sink",
        self.config["sink-filters"], true)
    enableFilters ("filters.configured.audio.source",
        self.config["source-filters"], true)
  else
    enableFilters ("filters.configured.audio.sink",
        self.config["sink-filters"], false)
    enableFilters ("filters.configured.audio.source",
        self.config["source-filters"], false)
  end
end

self.nodes_om:connect("objects-changed", function (om)
  checkFilters ()
end)

self.metadata_om:activate()
self.nodes_om:activate()
