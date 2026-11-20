# CustomItems mod

Gameplay mod (`client_only = false`): new weapons. Goal: the stock-crash bazooka (bouncing shell, harmless
blast, 8 red chart fragments, 10-69 damage per chart hit), in crates and the weapon panel. **Status: asset
pipeline done; the weapon itself is not.** Right now it only overrides `ClusterBomb.txt` so cluster
fragments use the chart model (test; remove when the real weapon exists).

## Files
- `customitems.cpp` - the DLL (no code yet).
- `bundles/CustomItems.bdl` - model bundle, loaded by the loader together with `Weapons.bdl`. **Generated**,
  contains data copied from the game's `Weapons.bdl` (shaders, two textures): keep the repo private.
- `files/scripts/scripts/equip/ClusterBomb.txt` - served by the loader in place of `scripts:/scripts/equip/ClusterBomb.txt`.
- `assets/make_bundle.py` - builds the bundle: copies the cluster bomblet's node tree (`3DClusterGrenade_Bomb`)
  per model, swaps mesh + diffuse/normal textures, registers it under a new name (`StockChart_Bomb`).
  `assets/chart.py` - the chart mesh (flat-shaded prisms) and solid-colour DXT5 textures; `python chart.py` self-checks.
- Uses `tools/xom.py` (XOM reader/writer).

Rebuild the bundle (game must be installed; venv python with no extra packages is enough):
```
python CustomItems/assets/make_bundle.py "H:\Games\SteamLibrary\steamapps\common\Worms Clan Wars" CustomItems/bundles/CustomItems.bdl
```

## Model facts (verified in game)
- Projectiles fly along model +Z; the camera sees them from -X (front = +X). The chart is built in x/y and
  rotated +90 deg about Y. Flat 2D objects that don't fly (physics props) face the camera with Z instead.
- Triangles are CCW seen from outside. Texture: u < 0.5 red, u >= 0.5 grey (one colour per 4x4 DXT block).
- Size ~4.5 units (cluster bomblet ~3).

## Test
Fire a cluster bomb: its 5 fragments are red falling charts. `WormsLoader.log` shows
`bundle load root:/Mods/CustomItems/bundles/CustomItems.bdl -> 0` and `file scripts:/scripts/equip/ClusterBomb.txt -> ...`.
