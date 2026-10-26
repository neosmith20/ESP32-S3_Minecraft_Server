// Which blocks the server supports, and how they behave.
//
// To add a block, add its vanilla name to `plainBlocks` (or one of the
// generated lists below) and run `node build_registries.js` again.
// Blocks with extra states (doors, stairs, ...) are recognized by name, see
// `familyOf`. Everything else is a simple block that has just one look.
//
// Block IDs in this server are the real Minecraft 1.21.8 block state IDs.

// ---------------------------------------------------------------- block list

const plainBlocks = [
  // Needed by terrain generation and the core game
  "air", "water", "lava",
  "stone", "cobblestone", "dirt", "grass_block", "sand", "sandstone", "mud",
  "ice", "snow", "snow_block", "bedrock",
  "oak_log", "oak_wood", "oak_planks", "oak_leaves", "oak_sapling",
  "dead_bush", "cactus", "lily_pad", "moss_carpet", "short_grass",
  "coal_ore", "iron_ore", "copper_ore", "gold_ore", "redstone_ore", "diamond_ore",
  "coal_block", "iron_block", "gold_block", "diamond_block", "redstone_block", "copper_block",
  // Utility
  "crafting_table", "furnace", "chest", "composter", "torch", "wall_torch", "lantern", "ladder",
  // Glass
  "glass", "glass_pane", "iron_bars",
  // Wooden things
  "oak_door", "oak_trapdoor", "oak_fence", "oak_fence_gate", "oak_button", "oak_pressure_plate",
  "stone_button", "stone_pressure_plate",
  // Stone building blocks
  "stone_bricks", "cracked_stone_bricks", "chiseled_stone_bricks", "smooth_stone",
  "smooth_sandstone", "cut_sandstone", "chiseled_sandstone"
];

// Materials that get stairs (every one of these must exist as "<name>_stairs")
const stairMaterials = ["oak", "cobblestone", "stone", "stone_brick", "sandstone", "smooth_sandstone"];
// Materials that get slabs
const slabMaterials = [
  "oak", "cobblestone", "stone", "smooth_stone", "stone_brick",
  "sandstone", "cut_sandstone", "smooth_sandstone"
];
// Materials that get walls
const wallMaterials = ["cobblestone", "stone_brick", "sandstone"];

const supportedBlocks = [
  ...plainBlocks,
  ...stairMaterials.map(m => m + "_stairs"),
  ...slabMaterials.map(m => m + "_slab"),
  ...wallMaterials.map(m => m + "_wall")
];

// ------------------------------------------------------------------ families

const DIRS = ["north", "south", "west", "east"];
const BOOL = ["false", "true"];

/**
 * Block "families" share how their states are chosen. For each family we
 * generate a lookup table of state offsets (relative to the block's first
 * state ID), indexed by the listed properties in that order. Values in the
 * `dims` lists are indexed 0, 1, 2... in the C code. `fixed` properties are
 * always set to the given value.
 */
const families = {
  DOOR: {
    sample: "oak_door", table: "door",
    dims: [["facing", DIRS], ["half", ["lower", "upper"]], ["open", BOOL]],
    fixed: { hinge: "left", powered: "false" }
  },
  TRAPDOOR: {
    sample: "oak_trapdoor", table: "trapdoor",
    dims: [["facing", DIRS], ["half", ["bottom", "top"]], ["open", BOOL]],
    fixed: { powered: "false", waterlogged: "false" }
  },
  GATE: {
    sample: "oak_fence_gate", table: "gate",
    dims: [["facing", DIRS], ["open", BOOL]],
    fixed: { in_wall: "false", powered: "false" }
  },
  STAIRS: {
    sample: "oak_stairs", table: "stairs",
    dims: [["facing", DIRS], ["half", ["bottom", "top"]]],
    fixed: { shape: "straight", waterlogged: "false" }
  },
  SLAB: {
    sample: "oak_slab", table: "slab",
    dims: [["type", ["bottom", "top", "double"]]],
    fixed: { waterlogged: "false" }
  },
  LOG: {
    sample: "oak_log", table: "log",
    dims: [["axis", ["x", "y", "z"]]],
    fixed: {}
  },
  LADDER: {
    sample: "ladder", table: "ladder",
    dims: [["facing", DIRS]],
    fixed: { waterlogged: "false" }
  },
  WALL_TORCH: {
    sample: "wall_torch", table: "wall_torch",
    dims: [["facing", DIRS]],
    fixed: {}
  },
  CHEST: {
    sample: "chest", table: "chest",
    dims: [["facing", DIRS]],
    fixed: { type: "single", waterlogged: "false" }
  },
  FURNACE: {
    sample: "furnace", table: "furnace",
    dims: [["facing", DIRS]],
    fixed: { lit: "false" }
  },
  LANTERN: {
    sample: "lantern", table: "lantern",
    dims: [["hanging", BOOL]],
    fixed: { waterlogged: "false" }
  },
  // Fences and panes: indexed [north][east][south][west], 1 = connected
  FENCE: {
    sample: "oak_fence", table: "fence",
    dims: [["north", BOOL], ["east", BOOL], ["south", BOOL], ["west", BOOL]],
    fixed: { waterlogged: "false" }
  },
  PANE: {
    sample: "glass_pane", table: "pane",
    dims: [["north", BOOL], ["east", BOOL], ["south", BOOL], ["west", BOOL]],
    fixed: { waterlogged: "false" }
  },
  // Walls: indexed [up][north][east][south][west], sides 0 = none, 1 = low
  WALL: {
    sample: "cobblestone_wall", table: "wall",
    dims: [
      ["up", BOOL],
      ["north", ["none", "low", "tall"]], ["east", ["none", "low", "tall"]],
      ["south", ["none", "low", "tall"]], ["west", ["none", "low", "tall"]]
    ],
    fixed: { waterlogged: "false" }
  }
};

function familyOf (name) {
  if (name === "water" || name === "lava") return "FLUID";
  if (name.endsWith("_door")) return "DOOR";
  if (name.endsWith("_trapdoor")) return "TRAPDOOR";
  if (name.endsWith("_fence_gate")) return "GATE";
  if (name.endsWith("_fence")) return "FENCE";
  if (name.endsWith("_stairs")) return "STAIRS";
  if (name.endsWith("_slab")) return "SLAB";
  if (name.endsWith("_wall")) return "WALL";
  if (name === "glass_pane" || name === "iron_bars") return "PANE";
  if (name === "oak_log" || name === "oak_wood") return "LOG";
  if (name === "ladder") return "LADDER";
  if (name === "wall_torch") return "WALL_TORCH";
  if (name === "torch") return "TORCH";
  if (name === "chest") return "CHEST";
  if (name === "furnace") return "FURNACE";
  if (name === "lantern") return "LANTERN";
  return "NONE";
}

// ---------------------------------------------------------------------- flags

// Item dropped by a block when it differs from the block's own item.
// Use null for "drops nothing".
const blockToItemOverrides = {
  "grass_block": "dirt",
  "stone": "cobblestone",
  "diamond_ore": "diamond",
  "gold_ore": "raw_gold",
  "redstone_ore": "redstone",
  "iron_ore": "raw_iron",
  "coal_ore": "coal",
  "copper_ore": "raw_copper",
  "snow": "snowball",
  "dead_bush": "stick",
  "wall_torch": "torch",
  "glass": null,
  "glass_pane": null,
  "ice": null,
  "oak_door": "oak_door"
};

// Blocks you can walk through (fluids and open doors are handled in code)
const passableBlocks = new Set([
  "air", "snow", "moss_carpet", "short_grass", "dead_bush", "torch", "wall_torch",
  "ladder", "oak_button", "stone_button", "oak_pressure_plate", "stone_pressure_plate"
]);
// Blocks that placed blocks may replace (fluids are handled in code)
const replaceableBlocks = new Set(["air", "short_grass", "snow"]);
// Blocks that break instantly
const instantBlocks = new Set(["dead_bush", "short_grass", "torch", "wall_torch", "lily_pad", "oak_sapling"]);
// Blocks that need something solid underneath
const columnBlocks = new Set([
  "snow", "moss_carpet", "cactus", "short_grass", "dead_bush", "sand", "torch",
  "oak_sapling", "oak_pressure_plate", "stone_pressure_plate"
]);
// Blocks that the client creates special rendering for only when told about
// them in a block update (light emitters and block entities)
const separateUpdateBlocks = new Set(["torch", "wall_torch", "chest", "lantern"]);
// Drops need a pickaxe (any kind)
function needsPickaxe (name) {
  if (name.endsWith("_ore")) return true;
  if (["coal_block", "iron_block", "gold_block", "diamond_block", "redstone_block", "copper_block"].includes(name)) return true;
  if (name === "furnace" || name === "iron_bars" || name === "lantern") return true;
  if (name === "stone_button" || name === "stone_pressure_plate") return true;
  if (/(^|_)(stone|cobblestone|stone_brick|stone_bricks|sandstone|smooth_stone)(_|$)/.test(name)) return true;
  if (/^(cracked_|chiseled_|cut_|smooth_)/.test(name)) return true;
  return false;
}
// Drops need an iron (or better) pickaxe
const ironPickaxeBlocks = new Set(["gold_ore", "redstone_ore", "diamond_ore"]);
// Drops need a shovel
const shovelBlocks = new Set(["snow"]);
// Full, solid cubes that fences, panes and walls connect to
const solidBlocks = new Set([
  "stone", "cobblestone", "dirt", "grass_block", "sand", "sandstone", "mud", "snow_block",
  "ice", "bedrock", "oak_log", "oak_wood", "oak_planks", "crafting_table", "furnace",
  "coal_ore", "iron_ore", "copper_ore", "gold_ore", "redstone_ore", "diamond_ore",
  "coal_block", "iron_block", "gold_block", "diamond_block", "redstone_block", "copper_block",
  "stone_bricks", "cracked_stone_bricks", "chiseled_stone_bricks", "smooth_stone",
  "smooth_sandstone", "cut_sandstone", "chiseled_sandstone"
]);

// Flag bits shared with the C code (see include/registries.h)
const FLAGS = {
  PASSABLE: 1,
  REPLACEABLE: 2,
  INSTAMINE: 4,
  COLUMN: 8,
  PICKAXE: 16,
  IRON_PICKAXE: 32,
  SHOVEL: 64,
  SEPARATE_UPDATE: 128,
  SOLID: 256,
  GLASS: 512
};

function flagsOf (name) {
  let f = 0;
  if (passableBlocks.has(name)) f |= FLAGS.PASSABLE;
  if (replaceableBlocks.has(name)) f |= FLAGS.REPLACEABLE;
  if (instantBlocks.has(name)) f |= FLAGS.INSTAMINE;
  if (columnBlocks.has(name)) f |= FLAGS.COLUMN;
  if (ironPickaxeBlocks.has(name)) f |= FLAGS.IRON_PICKAXE;
  else if (needsPickaxe(name) && !name.startsWith("oak_")) f |= FLAGS.PICKAXE;
  if (shovelBlocks.has(name)) f |= FLAGS.SHOVEL;
  if (separateUpdateBlocks.has(name)) f |= FLAGS.SEPARATE_UPDATE;
  if (solidBlocks.has(name)) f |= FLAGS.SOLID;
  if (name === "glass") f |= FLAGS.GLASS;
  return f;
}

// ----------------------------------------------------------------------- tags

// Which tool the client should use to mine each block (affects mining speed)
function toolTagOf (name) {
  if (name === "ice" || name === "iron_bars" || name === "lantern") return "mineable/pickaxe";
  const f = flagsOf(name);
  if (f & (FLAGS.PICKAXE | FLAGS.IRON_PICKAXE)) return "mineable/pickaxe";
  if (name.startsWith("oak_") && !name.endsWith("_leaves") && !name.endsWith("_sapling")) return "mineable/axe";
  if (name === "crafting_table" || name === "chest" || name === "ladder") return "mineable/axe";
  if (["grass_block", "dirt", "sand", "snow", "snow_block", "mud"].includes(name)) return "mineable/shovel";
  return null;
}

module.exports = {
  supportedBlocks, families, familyOf, flagsOf, FLAGS,
  blockToItemOverrides, toolTagOf, DIRS, BOOL
};
