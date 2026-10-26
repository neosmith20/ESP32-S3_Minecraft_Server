// Crafting recipes. To add one, add a line to the right section and run
// `node build_registries.js` again. Item names are the vanilla ones.
//
//   shaped(result, count, rows, key)   rows = pattern rows, "." = empty slot,
//                                      key = what each letter stands for.
//                                      Mirrored patterns match automatically.
//   shapeless(result, count, [items])  items in any order
//
// The special ingredient "#coal" means coal or charcoal.

const recipes = [];

function shaped (result, count, rows, key) {
  const w = rows[0].length, h = rows.length;
  if (rows.some(r => r.length !== w)) throw new Error("Uneven recipe rows for " + result);
  if (w > 3 || h > 3) throw new Error("Recipe too big for " + result);
  const input = [];
  for (const row of rows) {
    for (const ch of row) {
      if (ch === ".") input.push(null);
      else if (key[ch]) input.push(key[ch]);
      else throw new Error(`Recipe for ${result}: no key for "${ch}"`);
    }
  }
  recipes.push({ result, count, w, h, shapeless: false, input });
}

function shapeless (result, count, items) {
  recipes.push({ result, count, w: 0, h: 0, shapeless: true, input: items });
}

// -------------------------------------------------------------- tools, armor

const toolMaterials = {
  wooden: "oak_planks",
  stone: "cobblestone",
  iron: "iron_ingot",
  golden: "gold_ingot",
  diamond: "diamond",
  netherite: "netherite_ingot"
};
for (const [name, ingredient] of Object.entries(toolMaterials)) {
  const key = { M: ingredient, S: "stick" };
  shaped(`${name}_pickaxe`, 1, ["MMM", ".S.", ".S."], key);
  shaped(`${name}_axe`, 1, ["MM", "MS", ".S"], key);
  shaped(`${name}_shovel`, 1, ["M", "S", "S"], key);
  shaped(`${name}_sword`, 1, ["M", "M", "S"], key);
  shaped(`${name}_hoe`, 1, ["MM", ".S", ".S"], key);
}

const armorMaterials = {
  leather: "leather",
  iron: "iron_ingot",
  golden: "gold_ingot",
  diamond: "diamond",
  netherite: "netherite_ingot"
};
for (const [name, ingredient] of Object.entries(armorMaterials)) {
  const key = { M: ingredient };
  shaped(`${name}_helmet`, 1, ["MMM", "M.M"], key);
  shaped(`${name}_chestplate`, 1, ["M.M", "MMM", "MMM"], key);
  shaped(`${name}_leggings`, 1, ["MMM", "M.M", "M.M"], key);
  shaped(`${name}_boots`, 1, ["M.M", "M.M"], key);
}
shaped("bucket", 1, ["I.I", ".I."], { I: "iron_ingot" });
shaped("shears", 1, [".I", "I."], { I: "iron_ingot" });

// ------------------------------------------------------------ basic materials

shapeless("oak_planks", 4, ["oak_log"]);
shapeless("oak_planks", 4, ["oak_wood"]);
shaped("oak_wood", 3, ["LL", "LL"], { L: "oak_log" });
shaped("stick", 4, ["P", "P"], { P: "oak_planks" });
shaped("torch", 4, ["C", "S"], { C: "#coal", S: "stick" });
shaped("crafting_table", 1, ["PP", "PP"], { P: "oak_planks" });
shaped("chest", 1, ["PPP", "P.P", "PPP"], { P: "oak_planks" });
shaped("furnace", 1, ["CCC", "C.C", "CCC"], { C: "cobblestone" });
shaped("composter", 1, ["S.S", "S.S", "SSS"], { S: "oak_slab" });
shaped("ladder", 3, ["S.S", "SSS", "S.S"], { S: "stick" });
shaped("lantern", 1, ["NNN", "NTN", "NNN"], { N: "iron_nugget", T: "torch" });
shapeless("iron_nugget", 9, ["iron_ingot"]);
shaped("iron_ingot", 1, ["NNN", "NNN", "NNN"], { N: "iron_nugget" });
shaped("glass_pane", 16, ["GGG", "GGG"], { G: "glass" });
shaped("iron_bars", 16, ["III", "III"], { I: "iron_ingot" });
shaped("snow_block", 1, ["SS", "SS"], { S: "snowball" });

// Storage blocks (9 items <-> 1 block)
for (const [item, block] of [
  ["iron_ingot", "iron_block"], ["gold_ingot", "gold_block"], ["diamond", "diamond_block"],
  ["redstone", "redstone_block"], ["coal", "coal_block"], ["copper_ingot", "copper_block"]
]) {
  shaped(block, 1, ["III", "III", "III"], { I: item });
  shapeless(item, 9, [block]);
}

// ----------------------------------------------------------------------- food

shaped("bread", 1, ["WWW"], { W: "wheat" });

// -------------------------------------------------------------------- building

// Wooden things
shaped("oak_door", 3, ["PP", "PP", "PP"], { P: "oak_planks" });
shaped("oak_trapdoor", 2, ["PPP", "PPP"], { P: "oak_planks" });
shaped("oak_fence", 3, ["PSP", "PSP"], { P: "oak_planks", S: "stick" });
shaped("oak_fence_gate", 1, ["SPS", "SPS"], { P: "oak_planks", S: "stick" });
shapeless("oak_button", 1, ["oak_planks"]);
shaped("oak_pressure_plate", 1, ["PP"], { P: "oak_planks" });
shapeless("stone_button", 1, ["stone"]);
shaped("stone_pressure_plate", 1, ["SS"], { S: "stone" });

// Stone things
shaped("stone_bricks", 4, ["SS", "SS"], { S: "stone" });
shaped("chiseled_stone_bricks", 1, ["B", "B"], { B: "stone_brick_slab" });
shaped("sandstone", 1, ["SS", "SS"], { S: "sand" });
shaped("cut_sandstone", 4, ["SS", "SS"], { S: "sandstone" });
shaped("chiseled_sandstone", 1, ["S", "S"], { S: "sandstone_slab" });

// Stairs, slabs and walls
const stairSources = {
  oak_stairs: "oak_planks", cobblestone_stairs: "cobblestone", stone_stairs: "stone",
  stone_brick_stairs: "stone_bricks", sandstone_stairs: "sandstone",
  smooth_sandstone_stairs: "smooth_sandstone"
};
for (const [stairs, source] of Object.entries(stairSources)) {
  shaped(stairs, 4, ["M..", "MM.", "MMM"], { M: source });
}
const slabSources = {
  oak_slab: "oak_planks", cobblestone_slab: "cobblestone", stone_slab: "stone",
  smooth_stone_slab: "smooth_stone", stone_brick_slab: "stone_bricks",
  sandstone_slab: "sandstone", cut_sandstone_slab: "cut_sandstone",
  smooth_sandstone_slab: "smooth_sandstone", snow: "snow_block"
};
for (const [slab, source] of Object.entries(slabSources)) {
  shaped(slab, 6, ["MMM"], { M: source });
}
const wallSources = {
  cobblestone_wall: "cobblestone", stone_brick_wall: "stone_bricks", sandstone_wall: "sandstone"
};
for (const [wall, source] of Object.entries(wallSources)) {
  shaped(wall, 6, ["MMM", "MMM"], { M: source });
}

module.exports = { recipes };
