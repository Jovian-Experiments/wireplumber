-- uncomment to enable automatic filters policy logic
--
--[[

default_policy.filters = {
  -- The sink filters configuration
  ["sink-filters"] = {
    {
      ["stream-name"] = "output.virtual-sink",  -- loopback playback
      ["node-name"] = "input.virtual-sink",     -- loopback sink
      ["group"] = nil,  -- if nil, the default node will be used as target
      ["enabled"] = true,
      ["priority"] = 30,
    },
    {
      ["stream-name"] = "filter-chain-playback",  -- filter-chain playback
      ["node-name"] = "filter-chain-sink",        -- filter-chain sink
      ["group"] = "speakers",  -- if nil, the default node will be used as target
      ["enabled"] = true,
      ["priority"] = 20,
    },
    {
      ["stream-name"] = "echo-cancel-playback",  -- echo-cancel playback
      ["node-name"] = "echo-cancel-sink",        -- echo-cancel sink
      ["group"] = "speakers",  -- if nil, the default node will be used as target
      ["enabled"] = true,
      ["priority"] = 10,
    },
  },

  -- The source filters configuration
  ["source-filters"] = {
    {
      ["stream-name"] = "input.virtual-source",  -- loopback capture
      ["node-name"] = "output.virtual-source",   -- loopback source
      ["group"] = nil,  -- if nil, the default node will be used as target
      ["enabled"] = true,
      ["priority"] = 30,
    },
    {
      ["stream-name"] = "filter-chain-capture",  -- filter-chain capture
      ["node-name"] = "filter-chain-source",     -- filter-chain source
      ["group"] = "microphone",  -- if nil, the default node will be used as target
      ["enabled"] = true,
      ["priority"] = 20,
    },
    {
      ["stream-name"] = "echo-cancel-capture",  -- echo-cancel capture
      ["node-name"] = "echo-cancel-source",     -- echo-cancel source
      ["group"] = "microphone",  -- if nil, the default node will be used as target
      ["enabled"] = true,
      ["priority"] = 10,
    }
  },

  -- The final target node properties of each group
  ["groups-target"] = {
    ["speakers"] = {
      ["media.class"] = "Audio/Sink",
      ["alsa.card_name"] = "my-speakers-card-name",
    },
    ["microphone"] = {
      ["media.class"] = "Audio/Source",
      ["alsa.card_name"] = "my-microphone-card-name"
    }
  }
}
--]]
