-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-client")

config = {}
config.rules = Conf.get_section_as_json ("access.rules")

function getAccess (properties)
  local access = properties["pipewire.access"]
  local client_access = properties["pipewire.client.access"]
  local is_flatpak = properties["pipewire.sec.flatpak"]

  if is_flatpak then
    client_access = "flatpak"
  end

  if client_access == nil then
    return access
  elseif access == "unrestricted" or access == "default" then
    if client_access ~= "unrestricted" then
      return client_access
    end
  end

  return access
end

function getDefaultPermissions (properties)
  local access = properties["access"]
  local media_category = properties["media.category"]

  if access == "flatpak" and media_category == "Manager" then
    return "all", "flatpak-manager"
  elseif access == "flatpak" or access == "restricted" then
    return "rx", access
  elseif access == "default" then
    return "all", access
  end

  return nil, nil
end

function getPermissions (properties)
  if config.rules then
    local mprops, matched = JsonUtils.match_rules_update_properties (
        config.rules, properties)
    if (matched > 0 and mprops["default_permissions"]) then
      return mprops["default_permissions"], mprops["access"]
    end
  end

  return nil, nil
end

function updateObjectDefinedPermissions (client, properties, perms_table)
  if config.rules then
    local client_id = client["bound-id"]

    -- update perms_table with the permissions of each pipewire object
    JsonUtils.match_rules (config.rules, properties, function (action, value)
      if action == "update-perms" then
        for po in po_om:iterate () do
          local po_id = po["bound-id"]

	  -- make sure the pipewire object is not the client itself
	  if client_id ~= po_id then
            local po_properties = po["properties"]
            JsonUtils.match_rules (value, po_properties, function (po_action, po_value)
              if po_action == "permissions" then
                log:info (client, "Granting permissions to client " .. client_id ..
                    " on object " .. po_id .. " to '" .. po_value:parse() .. "'")
                perms_table[po_id] = po_value:parse()
              end

              return true
            end)
          end
        end
      end

      return true
    end)
  end
end

function handleClient (client)
  local id = client["bound-id"]
  local properties = client["properties"]
  local access = getAccess (properties)

  properties["access"] = access

  local perms, effective_access = getPermissions (properties)
  if perms == nil then
    perms, effective_access = getDefaultPermissions (properties)
  end
  if effective_access == nil then
    effective_access = access
  end

  if perms ~= nil then
    log:info(client, "Granting default permissions to client " .. id .. " (access " ..
      effective_access .. "): " .. perms)

    local perms_table = { ["any"] = perms }
    updateObjectDefinedPermissions (client, properties, perms_table)

    client:update_permissions (perms_table)
    client:update_properties { ["pipewire.access.effective"] = effective_access }
  else
    log:debug(client, "No rule for client " .. id .. " (access " .. access .. ")")
  end
end

po_om = ObjectManager {
  Interest { type = "PipewireObject" }
}

po_om:connect("objects-changed", function (om)
  for client in om:iterate { type = "client" } do
    handleClient (client)
  end
end)

po_om:activate()
