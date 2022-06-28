device_defaults = {}
device_defaults.enabled = true

device_defaults.properties = {
  -- store preferences to the file system and restore them at startup;
  -- when set to false, default nodes and routes are selected based on
  -- their priorities and any runtime changes do not persist after restart
  ["use-persistent-storage"] = true,

  -- the default volume to apply to ACP device nodes, in the linear scale
  --["default-volume"] = 0.4,

  -- Whether to auto-switch to echo cancel sink and source nodes or not
  ["auto-echo-cancel"] = true,

  -- Sets the default echo-cancel-sink node name to automatically switch to
  ["echo-cancel-sink-name"] = "echo-cancel-sink",

  -- Sets the default echo-cancel-source node name to automatically switch to
  ["echo-cancel-source-name"] = "echo-cancel-source",

  -- Whether to auto-switch to filter chain sink and source nodes or not
  ["auto-filter-chain"] = true,

  -- Sets the default filter-chain-sink node name to automatically switch to
  ["filter-chain-sink-name"] = "filter-chain-sink",

  -- Sets the default filter-chain-source node name to automatically switch to
  ["filter-chain-source-name"] = "filter-chain-source",

  -- Sets the timeout in seconds when the output alsa routes will be unmuted
  -- after resuming
  ["resume-mute-timeout"] = 10,
}

-- Sets persistent device profiles that should never change when wireplumber is
-- running, even if a new profile with higher priority becomes available
device_defaults.persistent_profiles = {
  {
    matches = {
      {
        -- Matches all devices
        { "device.name", "matches", "*" },
      },
    },
    profile_names = {
      "off",
      "pro-audio"
    }
  },
}

function device_defaults.enable()
  if device_defaults.enabled == false then
    return
  end

  -- Selects appropriate default nodes and enables saving and restoring them
  load_module("default-nodes", device_defaults.properties)

  -- API to listen for suspend/resume signals, needed to mute/unmute alsa
  -- output routes on suspend/resume if alsa sink nodes are not running. This
  -- Useful when we want to temporary mute alsa output if bluetooth device is
  -- being used when resuming
  load_module("login1-manager")

  -- Expose the actual default nodes to applications
  load_script("expose-default-nodes.lua", device_defaults.properties)

  -- Selects appropriate profile for devices
  load_script("policy-device-profile.lua", {
    persistent = device_defaults.persistent_profiles
  })

  -- Selects appropriate device routes ("ports" in pulseaudio terminology)
  -- and enables saving and restoring them together with
  -- their properties (per-route/port volume levels, channel maps, etc)
  load_script("policy-device-routes.lua", device_defaults.properties)

  if device_defaults.properties["use-persistent-storage"] then
    -- Enables functionality to save and restore default device profiles
    load_module("default-profile")
  end
end
