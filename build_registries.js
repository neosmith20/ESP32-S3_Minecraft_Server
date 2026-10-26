const fs = require("fs/promises");
const path = require("path");

const {
  supportedBlocks, families, familyOf, flagsOf, FLAGS,
  blockToItemOverrides, toolTagOf
} = require("./blocks_config.js");
const { recipes } = require("./recipes_config.js");

// Currently, only 4 biome types are supported, excluding "beach"
const biomes = [
  "plains",
  "mangrove_swamp",
  "desert",
  "snowy_plains",
  "beach"
];

// Family codes shared with the C code (see include/registries.h)
const familyNames = [
  "NONE", "FLUID", "DOOR", "TRAPDOOR", "GATE", "STAIRS", "SLAB", "LOG", "LADDER",
  "WALL_TORCH", "TORCH", "FENCE", "PANE", "WALL", "CHEST", "FURNACE", "LANTERN"
];

// Special ingredient groups used by recipes (item IDs are far below these)
const ingredientGroups = { "#coal": 0xF001 };

// Extract item and block data from the registry dump
async function extractItemsAndBlocks () {

  const blockSource = JSON.parse(await fs.readFile(`${__dirname}/notchian/generated/reports/blocks.json`, "utf8"));
  const registriesJSON = JSON.parse(await fs.readFile(`${__dirname}/notchian/generated/reports/registries.json`, "utf8"));
  const itemSource = registriesJSON["minecraft:item"].entries;
  const blockRegistrySource = registriesJSON["minecraft:block"].entries;

  const items = {};
  for (const item in itemSource) {
    items[item.replace("minecraft:", "")] = itemSource[item].protocol_id;
  }
  const blockRegistry = {};
  for (const block in blockRegistrySource) {
    blockRegistry[block.replace("minecraft:", "")] = blockRegistrySource[block].protocol_id;
  }

  // Describe every supported block
  const infos = [];
  for (const name of supportedBlocks) {
    const entry = blockSource["minecraft:" + name];
    if (!entry) throw new Error(`Block "${name}" does not exist in blocks.json`);
    const ids = entry.states.map(s => s.id);
    const min = Math.min(...ids), max = Math.max(...ids);
    if (max - min + 1 !== ids.length) throw new Error(`Block "${name}" has non-contiguous states`);
    const defaultState = entry.states.find(s => s.default);
    let dropName = name;
    if (name in blockToItemOverrides) dropName = blockToItemOverrides[name];
    const drop = dropName ? (items[dropName] || 0) : 0;
    if (name !== "air" && dropName && !(dropName in items) && name in blockToItemOverrides === false) {
      // Blocks without an item simply drop nothing
    }
    infos.push({
      name, min, max, entry,
      def: defaultState.id,
      item: name === "air" ? 0 : drop,
      ownItem: items[name] || 0,
      family: familyOf(name),
      flags: flagsOf(name)
    });
  }
  infos.sort((a, b) => a.min - b.min);

  return { items, blockRegistry, infos, blockSource };

}

// Find the state offset (from the block's first state) with the given properties
function stateOffset (info, props) {
  const state = info.entry.states.find(s => Object.keys(props).every(k => s.properties && s.properties[k] === props[k]));
  if (!state) throw new Error(`No state of ${info.name} matches ${JSON.stringify(props)}`);
  return state.id - info.min;
}

// Builds a nested offset table for a family. Returns { dims, text, lookup }
function buildFamilyTable (famName, fam, infos) {

  const sample = infos.find(i => i.name === fam.sample);
  if (!sample) throw new Error(`Family ${famName}: sample block ${fam.sample} is not supported`);
  const members = infos.filter(i => i.family === famName);

  const build = (info, level, props) => {
    if (level === fam.dims.length) return stateOffset(info, { ...fam.fixed, ...props });
    const [prop, values] = fam.dims[level];
    return values.map(v => build(info, level + 1, { ...props, [prop]: v }));
  };
  const table = build(sample, 0, {});

  // All members of a family must use the same layout, check that
  for (const member of members) {
    const other = build(member, 0, {});
    if (JSON.stringify(other) !== JSON.stringify(table)) {
      throw new Error(`Block ${member.name} does not match the state layout of ${fam.sample}`);
    }
  }

  const dims = fam.dims.map(d => d[1].length);
  const text = JSON.stringify(table).replace(/\[/g, "{").replace(/\]/g, "}");
  return { dims, text, table };

}

// Write an integer as a VarInt
function writeVarInt (value) {
  const bytes = [];
  while (true) {
    if ((value & ~0x7F) === 0) {
      bytes.push(value);
      return Buffer.from(bytes);
    }
    bytes.push((value & 0x7F) | 0x80);
    value >>>= 7;
  }
}

// Scan directory recursively to find all JSON files
async function scanDirectory (basePath, currentPath = "") {
  const entries = {};
  const items = await fs.readdir(path.join(basePath, currentPath), { withFileTypes: true });

  for (const item of items) {
    const relativePath = path.join(currentPath, item.name);
    if (item.isDirectory()) {
      const subEntries = await scanDirectory(basePath, relativePath);
      Object.assign(entries, subEntries);
    } else if (item.name.endsWith(".json")) {
      const dirPath = path.dirname(relativePath);
      const registryName = dirPath === "." ? "" : dirPath;
      const entryName = path.basename(item.name, ".json");

      if (!entries[registryName]) {
        entries[registryName] = [];
      }
      entries[registryName].push(entryName);
    }
  }

  return entries;
}

// Serialize a single registry
function serializeRegistry (name, entries) {
  const parts = [];

  // Packet ID for Registry Data
  parts.push(Buffer.from([0x07]));

  // Registry name
  const nameBuf = Buffer.from(name, "utf8");
  parts.push(writeVarInt(nameBuf.length));
  parts.push(nameBuf);

  // Entry count
  parts.push(writeVarInt(entries.length));

  // Serialize entries
  for (const entryName of entries) {
    const entryBuf = Buffer.from(entryName, "utf8");
    parts.push(writeVarInt(entryBuf.length));
    parts.push(entryBuf);
    parts.push(Buffer.from([0x00]));
  }

  // Combine all parts
  const fullData = Buffer.concat(parts);

  // Prepend packet length
  const lengthBuf = writeVarInt(fullData.length);

  return Buffer.concat([lengthBuf, fullData]);
}

// Serialize a tag update
function serializeTags (tags) {
  const parts = [];

  // Packet ID for Update Tags
  parts.push(Buffer.from([0x0D]));

  // Tag type count
  parts.push(writeVarInt(Object.keys(tags).length));

  // Tag registry entry
  for (const type in tags) {

    // Tag registry identifier
    const identifier = Buffer.from(type, "utf8");
    parts.push(writeVarInt(identifier.length));
    parts.push(identifier);

    // Tag count
    parts.push(writeVarInt(Object.keys(tags[type]).length));

    // Write tag data
    for (const tag in tags[type]) {
      // Tag identifier
      const identifier = Buffer.from(tag, "utf8");
      parts.push(writeVarInt(identifier.length));
      parts.push(identifier);
      // Array of IDs
      parts.push(writeVarInt(Object.keys(tags[type][tag]).length));
      for (const id of tags[type][tag]) {
        parts.push(writeVarInt(id));
      }
    }

  }

  // Combine all parts
  const fullData = Buffer.concat(parts);

  // Prepend packet length
  const lengthBuf = writeVarInt(fullData.length);

  return Buffer.concat([lengthBuf, fullData]);
}

function toVarIntBuffer (array) {
  const parts = [];
  for (const num of array) {
    parts.push(writeVarInt(num));
  }
  return Buffer.concat(parts);
}

// Convert to C-style hex byte array string
function toCArray (buffer) {
  const hexBytes = [...buffer].map(b => `0x${b.toString(16).padStart(2, "0")}`);
  const lines = [];
  for (let i = 0; i < hexBytes.length; i += 12) {
    lines.push("  " + hexBytes.slice(i, i + 12).join(", "));
  }
  return lines.join(",\n");
}

const requiredRegistries = [
  "cat_variant",
  "chicken_variant",
  "cow_variant",
  "frog_variant",
  "painting_variant",
  "pig_variant",
  "wolf_sound_variant",
  "wolf_variant",
  "damage_type"
];


async function convert () {

  const inputPath = __dirname + "/notchian/generated/data/minecraft";
  const outputPath = __dirname + "/src/registries.c";
  const headerPath = __dirname + "/include/registries.h";

  const registries = await scanDirectory(inputPath);
  const registryBuffers = [];

  for (const registry of requiredRegistries) {
    if (!(registry in registries)) {
      console.error(`Missing required registry "${registry}"!`);
      return;
    }
    if (registry.endsWith("variant")) {
      // The mob "variants" only require one valid variant to be accepted
      // Send "temperate" if available, otherwise shortest string to save memory
      if (registries[registry].includes("temperate")) {
        registryBuffers.push(serializeRegistry(registry, ["temperate"]));
      } else {
        const shortest = registries[registry].sort((a, b) => a.length - b.length)[0];
        registryBuffers.push(serializeRegistry(registry, [shortest]));
      }
    } else {
      registryBuffers.push(serializeRegistry(registry, registries[registry]));
    }
  }
  // Send biomes separately - only "plains" is actually required
  registryBuffers.push(serializeRegistry("worldgen/biome", biomes));
  // Send dimensions separately - we only use "overworld"
  registryBuffers.push(serializeRegistry("dimension_type", ["overworld"]));
  const fullRegistryBuffer = Buffer.concat(registryBuffers);

  const { items, blockRegistry, infos } = await extractItemsAndBlocks();

  // Tool tags tell the client how fast blocks can be mined
  const tagLists = { "mineable/pickaxe": [], "mineable/axe": [], "mineable/shovel": [] };
  for (const info of infos) {
    const tag = toolTagOf(info.name);
    if (tag && blockRegistry[info.name] !== undefined) tagLists[tag].push(blockRegistry[info.name]);
  }

  const tagBuffer = serializeTags({
    "fluid": {
      // Water and lava, both flowing and still states
      "water": [ 1, 2 ],
      "lava": [ 3, 4 ]
    },
    "block": {
      ...tagLists,
      "leaves": [ blockRegistry["oak_leaves"] ]
    },
    "item": {
      "planks": [ items["oak_planks"] ]
    }
  });

  // Offset tables for blocks with several states
  const familyTables = {};
  for (const famName in families) {
    familyTables[famName] = buildFamilyTable(famName, families[famName], infos);
  }

  // Recipes
  const resolve = (name) => {
    if (name in ingredientGroups) return ingredientGroups[name];
    if (!(name in items)) throw new Error(`Recipe uses unknown item "${name}"`);
    return items[name];
  };
  const recipeRows = recipes.map(r => {
    const input = r.input.map(c => c === null ? 0 : resolve(c));
    while (input.length < 9) input.push(0);
    return `  { ${resolve(r.result)}, ${r.count}, ${r.w}, ${r.h}, ${r.shapeless ? 1 : 0}, { ${input.join(", ")} } }, // ${r.result}`;
  });

  // Block info table, sorted by first state ID
  const infoRows = infos.map(i =>
    `  { ${i.min}, ${i.max}, ${i.item}, FAM_${i.family}, ${i.flags} }, // ${i.name}`
  );

  // Item-to-block mapping: placing an item places the block's default state
  const itemToBlock = new Map();
  for (const info of infos) {
    if (!info.ownItem || info.family === "FLUID" || info.family === "WALL_TORCH" || info.name === "air") continue;
    if (!itemToBlock.has(info.ownItem)) itemToBlock.set(info.ownItem, info.def);
  }

  const grass = infos.find(i => i.name === "grass_block");
  const snowyGrass = grass.entry.states.find(s => s.properties && s.properties.snowy === "true").id;

  const familyEnum = familyNames.map((n, i) => `#define FAM_${n} ${i}`).join("\n");
  const flagDefs = Object.entries(FLAGS).map(([n, v]) => `#define BF_${n} ${v}`).join("\n");

  const sourceCode = `\
#include <stdint.h>
#include "registries.h"

// Binary contents of required "Registry Data" packets
uint8_t registries_bin[] = {
${toCArray(fullRegistryBuffer)}
};
// Binary contents of "Update Tags" packets
uint8_t tags_bin[] = {
${toCArray(tagBuffer)}
};

// Info about every supported block, sorted by first state ID
const BlockInfo block_info[] = {
${infoRows.join("\n")}
};
const uint16_t block_info_count = ${infos.length};

// Placing an item places this block state (0 = not a block)
uint16_t I_to_B (uint16_t item) {
  switch (item) {
    ${[...itemToBlock.entries()].map(([item, state]) => `case ${item}: return ${state};`).join("\n    ")}
    default: break;
  }
  return 0;
}

// State layout tables of block families
${Object.entries(familyTables).map(([name, t]) =>
  `const uint16_t OFF_${families[name].table}${t.dims.map(d => `[${d}]`).join("")} = ${t.text};`
).join("\n")}

// Crafting recipes
const Recipe recipes[] = {
${recipeRows.join("\n")}
};
const uint16_t recipe_count = ${recipes.length};
`;

  const headerCode = `\
#ifndef H_REGISTRIES
#define H_REGISTRIES

#include <stdint.h>

// Binary packet data (${fullRegistryBuffer.length + tagBuffer.length} bytes total)
extern uint8_t registries_bin[${fullRegistryBuffer.length}];
extern uint8_t tags_bin[${tagBuffer.length}];

// Block families (how a block's states are chosen)
${familyEnum}

// Block flags
${flagDefs}

// Describes a block: all of its states lie in [min; max]
typedef struct {
  uint16_t min;
  uint16_t max;
  uint16_t item; // item dropped when mined (0 = nothing)
  uint8_t family;
  uint16_t flags;
} BlockInfo;
extern const BlockInfo block_info[];
extern const uint16_t block_info_count;

uint16_t I_to_B (uint16_t item); // Item-to-block mapping

// Crafting recipes. For shaped recipes the ingredients are given row by row
// for a w x h area, shapeless recipes list them in any order.
#define RECIPE_GROUP_COAL 0xF001
typedef struct {
  uint16_t result;
  uint8_t count;
  uint8_t w;
  uint8_t h;
  uint8_t shapeless;
  uint16_t input[9];
} Recipe;
extern const Recipe recipes[];
extern const uint16_t recipe_count;

// State layout tables, see blocks_config.js
${Object.entries(familyTables).map(([name, t]) =>
  `extern const uint16_t OFF_${families[name].table}${t.dims.map(d => `[${d}]`).join("")};`
).join("\n")}

// Block identifiers: the default state of each block, and the range of
// states it covers. These are real Minecraft block state IDs.
${infos.map(i => `#define B_${i.name} ${i.def}`).join("\n")}
#define B_snowy_grass_block ${snowyGrass}
${infos.map(i => `#define BMIN_${i.name} ${i.min}\n#define BMAX_${i.name} ${i.max}`).join("\n")}
// True if the block state belongs to the given block (e.g. IS_BLOCK(s, oak_door))
#define IS_BLOCK(state, name) ((state) >= BMIN_##name && (state) <= BMAX_##name)

// Item identifiers
${Object.entries(items).map(c => `#define I_${c[0]} ${c[1]}`).join("\n")}

// Biome identifiers
${biomes.map((c, i) => `#define W_${c} ${i}`).join("\n")}

// Damage type identifiers
${registries["damage_type"].map((c, i) => `#define D_${c} ${i}`).join("\n")}

#endif
`;

  await fs.writeFile(outputPath, sourceCode);
  await fs.writeFile(headerPath, headerCode);
  console.log(`Done. Wrote to \`registries.c\` and \`registries.h\` (${infos.length} blocks, ${recipes.length} recipes)`);

}

convert().catch(e => { console.error(e); process.exit(1); });
