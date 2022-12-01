-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

local config = ... or {}

-- preprocess rules and create Interest objects
for _, r in ipairs(config.rules or {}) do
  r.interests = {}
  for _, i in ipairs(r.matches) do
    local interest_desc = { type = "properties" }
    for _, c in ipairs(i) do
      c.type = "pw"
      table.insert(interest_desc, Constraint(c))
    end
    local interest = Interest(interest_desc)
    table.insert(r.interests, interest)
  end
  r.matches = nil
end

function rulesGetDefaultPermissions(properties)
  for _, r in ipairs(config.rules or {}) do
    if r.default_permissions then
      for _, interest in ipairs(r.interests) do
        if interest:matches(properties) then
          return r.default_permissions
        end
      end
    end
  end
end

nodes_om = ObjectManager { Interest { type = "node" } }
nodes_om:activate()

clients_om = ObjectManager {
  Interest { type = "client" }
}

clients_om:connect("object-added", function (om, client)
  local id = client["bound-id"]
  local properties = client["properties"]

  -- do not give any permission to filter nodes if client is pipewire-pulse
  if properties["config.name"] == "pipewire-pulse.conf" then
    local perms_table = { ["any"] = "all" }

    for n in nodes_om:iterate() do
      local node_id = n["bound-id"]
      local node_properties = n["properties"]
      if node_properties["node.name"] == "echo-cancel-sink" or
          node_properties["node.name"] == "echo-cancel-source" or
          node_properties["node.name"] == "echo-cancel-playback" or
          node_properties["node.name"] == "echo-cancel-capture" or
          node_properties["node.name"] == "filter-chain-sink" or
          node_properties["node.name"] == "filter-chain-source" or
          node_properties["node.name"] == "filter-chain-playback" or
          node_properties["node.name"] == "filter-chain-capture" or
          node_properties["node.name"] == "input.virtual-source" then
        Log.info (client, "Removing permissions to client " .. id .. " in node " .. node_id)
        perms_table[node_id] = ""
      end
    end

    Log.info(client, "Granting permissions to client " .. id .. ": " .. perms_table["any"])
    client:update_permissions (perms_table)
    return
  end


  local perms = rulesGetDefaultPermissions(properties)
  if perms then
    Log.info(client, "Granting permissions to client " .. id .. ": " .. perms)
    client:update_permissions { ["any"] = perms }
  end
end)

clients_om:activate()
