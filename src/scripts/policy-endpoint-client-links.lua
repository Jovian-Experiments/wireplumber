-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

local config = ...
config.roles = config.roles or {}

function findRole(role)
  if role and not config.roles[role] then
    for r, p in pairs(config.roles) do
      if type(p.alias) == "table" then
        for i = 1, #(p.alias), 1 do
          if role == p.alias[i] then
            return r
          end
        end
      end
    end
  end
  return role
end

function priorityForRole(role)
  local r = role and config.roles[role] or nil
  return r and r.priority or 0
end

function getAction(dominant_role, other_role)
  -- default to "mix" if the role is not configured
  if not dominant_role or not config.roles[dominant_role] then
    return "mix"
  end

  local role_config = config.roles[dominant_role]
  return role_config["action." .. other_role]
      or role_config["action.default"]
      or "mix"
end

function rescan()
  local links = {
    ["Audio/Source"] = {},
    ["Audio/Sink"] = {},
    ["Video/Source"] = {},
  }

  Log.debug("Rescan endpoint links")

  -- gather info about links
  for silink in silinks_om:iterate() do
    local props = silink.properties
    local role = props["media.role"]
    local target_class = props["target.media.class"]
    local plugged = props["item.plugged.usec"]
    local active =
      ((silink:get_active_features() & Feature.SessionItem.ACTIVE) ~= 0)
    if links[target_class] then
      table.insert(links[target_class], {
        silink = silink,
        role = findRole(role),
        active = active,
        priority = priorityForRole(role),
        plugged = plugged and tonumber(plugged) or 0
      })
    end
  end

  local function compareLinks(l1, l2)
    return (l1.priority > l2.priority) or
        ((l1.priority == l2.priority) and (l1.plugged > l2.plugged))
  end

  for k, v in pairs(links) do
    -- sort on priority and stream creation time
    table.sort(v, compareLinks)

    -- apply actions
    local first_link = v[1]
    if first_link then
      for i = 2, #v, 1 do
        local action = getAction(first_link.role, v[i].role)
        if action == "cork" then
          if v[i].active then
            v[i].silink:deactivate(Feature.SessionItem.ACTIVE)
          end
        elseif action == "mix" then
          if not v[i].active then
            v[i].silink:activate(Feature.SessionItem.ACTIVE, pendingOperation())
          end
        -- elseif action == "duck" then
        --   TODO
        else
          Log.warning("Unknown action: " .. action)
        end
      end

      if not first_link.active then
        first_link.silink:activate(Feature.SessionItem.ACTIVE, pendingOperation())
      end
    end
  end
end

pending_ops = 0
pending_rescan = false

function pendingOperation()
  pending_ops = pending_ops + 1
  return function()
    pending_ops = pending_ops - 1
    if pending_ops == 0 and pending_rescan then
      pending_rescan = false
      rescan()
    end
  end
end

function maybeRescan()
  if pending_ops == 0 then
    rescan()
  else
    pending_rescan = true
  end
end

silinks_om = ObjectManager {
  Interest {
    type = "SiLink",
    Constraint { "is.policy.endpoint.client.link", "=", true },
  },
}
silinks_om:connect("objects-changed", maybeRescan)
silinks_om:activate()